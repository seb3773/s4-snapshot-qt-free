#include "embedded_assets.h"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <unistd.h>
#include <fstream>
#include <vector>

extern "C" {
#include "codec_lz4.h"
#include "microtar.h"
#include "mtar_memory.h"
}

namespace {

bool starts_with(const std::string &value, const std::string &prefix)
{
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

std::string join_path(const std::string &root, const std::string &rel)
{
    if (rel.empty()) {
        return root;
    }
    return (std::filesystem::path(root) / rel).string();
}

bool ensure_parent_dir(const std::string &path, std::string &error)
{
    const std::filesystem::path parent = std::filesystem::path(path).parent_path();
    if (parent.empty()) {
        return true;
    }

    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) {
        error = std::string("cannot create directory '") + parent.string() + "': " + ec.message();
        return false;
    }
    return true;
}

bool should_skip_tar_type(char type)
{
    return type == '\0' || type == 'g' || type == 'x';
}

bool is_directory_entry(const mtar_header_t &header)
{
    if (header.type == MTAR_TDIR) {
        return true;
    }
    const std::size_t name_len = std::strlen(header.name);
    return name_len > 0 && header.name[name_len - 1] == '/';
}

} // namespace

EmbeddedAssets::Result EmbeddedAssets::decompressPayload(const EmbeddedPayloadView &payload,
                                                         std::vector<std::uint8_t> &out)
{
    Result result;
    if (payload.data == nullptr || payload.compressed_size == 0 || payload.uncompressed_size == 0) {
        result.error = "embedded payload is empty";
        return result;
    }

    out.assign(payload.uncompressed_size, 0);
    const int decoded = lz4_decompress(reinterpret_cast<const char *>(payload.data),
                                       reinterpret_cast<char *>(out.data()),
                                       static_cast<int>(payload.compressed_size),
                                       static_cast<int>(payload.uncompressed_size));
    if (decoded != static_cast<int>(payload.uncompressed_size)) {
        result.error = "lz4 decompression failed";
        return result;
    }

    result.ok = true;
    return result;
}

EmbeddedAssets::Result EmbeddedAssets::extractTar(const std::vector<std::uint8_t> &tar_data,
                                                  const EmbeddedTarExtractOptions &options)
{
    Result result;
    if (tar_data.empty()) {
        result.error = "tar buffer is empty";
        return result;
    }
    if (options.dest_root.empty()) {
        result.error = "destination root is empty";
        return result;
    }

    std::error_code ec;
    std::filesystem::create_directories(options.dest_root, ec);
    if (ec) {
        result.error = std::string("cannot create destination root '") + options.dest_root + "': " + ec.message();
        return result;
    }

    mtar_t tar;
    if (mtar_open_memory(&tar, tar_data.data(), static_cast<unsigned>(tar_data.size())) != MTAR_ESUCCESS) {
        result.error = "cannot open in-memory tar stream";
        return result;
    }

    if (mtar_rewind(&tar) != MTAR_ESUCCESS) {
        mtar_close(&tar);
        result.error = "cannot rewind in-memory tar stream";
        return result;
    }

    std::vector<char> buffer(64 * 1024);

    for (;;) {
        mtar_header_t header;
        const int header_err = mtar_read_header(&tar, &header);
        if (header_err == MTAR_ENULLRECORD) {
            break;
        }
        if (header_err != MTAR_ESUCCESS) {
            mtar_close(&tar);
            result.error = std::string("tar header read failed: ") + mtar_strerror(header_err);
            return result;
        }

        std::string entry_name = header.name;
        if (!options.entry_prefix.empty() && !starts_with(entry_name, options.entry_prefix)) {
            if (mtar_next(&tar) != MTAR_ESUCCESS) {
                break;
            }
            continue;
        }

        if (!options.strip_prefix.empty()) {
            if (!starts_with(entry_name, options.strip_prefix)) {
                if (mtar_next(&tar) != MTAR_ESUCCESS) {
                    break;
                }
                continue;
            }
            entry_name.erase(0, options.strip_prefix.size());
        }

        if (entry_name.empty() || entry_name == "." || entry_name == "./") {
            if (mtar_next(&tar) != MTAR_ESUCCESS) {
                break;
            }
            continue;
        }

        if (should_skip_tar_type(static_cast<char>(header.type))) {
            if (mtar_next(&tar) != MTAR_ESUCCESS) {
                break;
            }
            continue;
        }

        const std::string out_path = join_path(options.dest_root, entry_name);

        if (is_directory_entry(header)) {
            std::filesystem::create_directories(out_path, ec);
            if (ec) {
                mtar_close(&tar);
                result.error = std::string("cannot create directory '") + out_path + "': " + ec.message();
                return result;
            }
        } else if (header.type == MTAR_TREG) {
            if (!ensure_parent_dir(out_path, result.error)) {
                mtar_close(&tar);
                return result;
            }

            unsigned remaining = header.size;
            std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
            if (!out) {
                mtar_close(&tar);
                result.error = std::string("cannot open '") + out_path + "' for writing";
                return result;
            }

            while (remaining > 0) {
                const unsigned chunk = remaining > buffer.size() ? static_cast<unsigned>(buffer.size()) : remaining;
                if (mtar_read_data(&tar, buffer.data(), chunk) != MTAR_ESUCCESS) {
                    mtar_close(&tar);
                    result.error = std::string("tar data read failed for '") + entry_name + "'";
                    return result;
                }
                if (remaining > 0 && out) {
                    out.write(buffer.data(), static_cast<std::streamsize>(chunk));
                    if (!out) {
                        mtar_close(&tar);
                        result.error = std::string("short write on '") + out_path + "'";
                        return result;
                    }
                }
                remaining -= chunk;
            }
            std::filesystem::permissions(out_path,
                                         static_cast<std::filesystem::perms>(header.mode & 07777),
                                         std::filesystem::perm_options::replace,
                                         ec);
            if (ec) {
                mtar_close(&tar);
                result.error = std::string("cannot set permissions on '") + out_path + "': " + ec.message();
                return result;
            }
        } else if (header.type == MTAR_TSYM) {
            if (!ensure_parent_dir(out_path, result.error)) {
                mtar_close(&tar);
                return result;
            }
            if (std::filesystem::exists(out_path, ec)) {
                std::filesystem::remove(out_path, ec);
            }
            std::string link_target = header.linkname;
            if (link_target.empty() && header.size > 0) {
                std::vector<char> link_data(header.size + 1, '\0');
                if (mtar_read_data(&tar, link_data.data(), header.size) != MTAR_ESUCCESS) {
                    mtar_close(&tar);
                    result.error = std::string("tar symlink target read failed for '") + entry_name + "'";
                    return result;
                }
                link_target = link_data.data();
            }
            if (link_target.empty()) {
                mtar_close(&tar);
                result.error = std::string("empty symlink target for '") + entry_name + "'";
                return result;
            }
            if (::symlink(link_target.c_str(), out_path.c_str()) != 0) {
                mtar_close(&tar);
                result.error = std::string("cannot create symlink '") + out_path + "': " + std::strerror(errno);
                return result;
            }
        } else if (header.type == MTAR_TLNK) {
            if (!ensure_parent_dir(out_path, result.error)) {
                mtar_close(&tar);
                return result;
            }
            if (std::filesystem::exists(out_path, ec)) {
                std::filesystem::remove(out_path, ec);
            }
            const std::filesystem::path link_path = std::filesystem::path(header.linkname).is_absolute()
                                                      ? std::filesystem::path(header.linkname)
                                                      : (std::filesystem::path(out_path).parent_path() / header.linkname);
            if (::link(link_path.c_str(), out_path.c_str()) != 0) {
                mtar_close(&tar);
                result.error = std::string("cannot create hard link '") + out_path + "': " + std::strerror(errno);
                return result;
            }
        }

        if (mtar_next(&tar) != MTAR_ESUCCESS) {
            break;
        }
    }

    mtar_close(&tar);
    result.ok = true;
    return result;
}

EmbeddedAssets::Result EmbeddedAssets::extractPayload(const EmbeddedPayloadView &payload,
                                                      const EmbeddedTarExtractOptions &options)
{
    std::vector<std::uint8_t> tar_data;
    Result result = decompressPayload(payload, tar_data);
    if (!result.ok) {
        return result;
    }
    return extractTar(tar_data, options);
}

EmbeddedAssets::Result EmbeddedAssets::extractLiveFiles(const std::string &dest_root)
{
    EmbeddedTarExtractOptions options;
    options.dest_root = dest_root;
    options.strip_prefix = "live-files/";
    return extractPayload(liveFilesPayload(), options);
}

EmbeddedAssets::Result EmbeddedAssets::extractIsoTemplates(const std::string &dest_root)
{
    EmbeddedTarExtractOptions options;
    options.dest_root = dest_root;
    options.strip_prefix = "iso-templates/";
    return extractPayload(isoTemplatesPayload(), options);
}

EmbeddedAssets::Result EmbeddedAssets::extractRuntimeScripts(const std::string &dest_root)
{
    EmbeddedTarExtractOptions options;
    options.dest_root = dest_root;
    options.strip_prefix = "runtime-scripts/";
    return extractPayload(runtimeScriptsPayload(), options);
}

EmbeddedAssets::Result EmbeddedAssets::extractIsoTemplateTree(const std::string &dest_root)
{
    EmbeddedTarExtractOptions options;
    options.dest_root = dest_root;
    options.entry_prefix = "iso-templates/iso-template/";
    options.strip_prefix = "iso-templates/iso-template/";
    return extractPayload(isoTemplatesPayload(), options);
}

EmbeddedAssets::Result EmbeddedAssets::extractTemplateInitrd(const std::string &dest_root)
{
    EmbeddedTarExtractOptions options;
    options.dest_root = dest_root;
    options.entry_prefix = "iso-templates/template-initrd/";
    options.strip_prefix = "iso-templates/template-initrd/";
    return extractPayload(isoTemplatesPayload(), options);
}
