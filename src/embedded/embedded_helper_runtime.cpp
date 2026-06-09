#include "embedded_helper_runtime.h"

#include "embedded_assets.h"
#include "helper_binary_payload.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

std::string g_preferred_helper_path;

std::string getenv_non_empty(const char *name)
{
    const char *value = std::getenv(name);
    return (value && *value) ? std::string(value) : std::string();
}

std::string helper_state_base_dir()
{
    const std::string xdg_runtime = getenv_non_empty("XDG_RUNTIME_DIR");
    if (!xdg_runtime.empty() && xdg_runtime.front() == '/') {
        return (std::filesystem::path(xdg_runtime) / "s4-snapshot").string();
    }

    return (std::filesystem::path("/tmp") / ("s4-snapshot-" + std::to_string(static_cast<long long>(::getuid())))).string();
}

std::string fallback_helper_path()
{
    return (std::filesystem::path(helper_state_base_dir()) / "helper" / "helper").string();
}

} // namespace

void EmbeddedHelperRuntime::setPreferredHelperPath(const std::string &path)
{
    g_preferred_helper_path = path;
}

std::string EmbeddedHelperRuntime::defaultHelperPathForWorkDir(const std::string &workDir)
{
    return (std::filesystem::path(workDir) / "_embedded" / "helper" / "helper").string();
}

std::string EmbeddedHelperRuntime::helperStateBaseDir()
{
    return helper_state_base_dir();
}

std::string EmbeddedHelperRuntime::runtimeScriptsDir()
{
    return (std::filesystem::path(helper_state_base_dir()) / "runtime-scripts").string();
}

EmbeddedHelperRuntime::Result EmbeddedHelperRuntime::ensureHelperAvailable()
{
    return ensureHelperAvailableAt(g_preferred_helper_path.empty() ? fallback_helper_path() : g_preferred_helper_path);
}

EmbeddedHelperRuntime::Result EmbeddedHelperRuntime::ensureHelperAvailableAt(const std::string &path)
{
    Result result;
    if (path.empty()) {
        result.error = "embedded helper destination path is empty";
        return result;
    }

    std::error_code ec;
    const std::filesystem::path outPath(path);
    std::filesystem::create_directories(outPath.parent_path(), ec);
    if (ec) {
        result.error = std::string("cannot create embedded helper directory: ") + ec.message();
        return result;
    }

    const std::filesystem::path tmpPath = outPath.string() + ".tmp";
    std::vector<std::uint8_t> helperData;
    const EmbeddedPayloadView payload{
        embedded_helper_binary_payload,
        embedded_helper_binary_compressed_size,
        embedded_helper_binary_uncompressed_size,
    };
    const EmbeddedAssets::Result decompress = EmbeddedAssets::decompressPayload(payload, helperData);
    if (!decompress.ok) {
        result.error = std::string("cannot decompress embedded helper: ") + decompress.error;
        return result;
    }

    {
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            result.error = std::string("cannot open embedded helper destination: ") + tmpPath.string();
            return result;
        }
        out.write(reinterpret_cast<const char *>(helperData.data()), static_cast<std::streamsize>(helperData.size()));
        if (!out) {
            result.error = std::string("cannot write embedded helper destination: ") + tmpPath.string();
            return result;
        }
    }

    std::filesystem::permissions(tmpPath,
                                 std::filesystem::perms::owner_read | std::filesystem::perms::owner_write
                                     | std::filesystem::perms::owner_exec,
                                 std::filesystem::perm_options::replace,
                                 ec);
    if (ec) {
        result.error = std::string("cannot chmod embedded helper: ") + ec.message();
        return result;
    }

    std::filesystem::rename(tmpPath, outPath, ec);
    if (ec) {
        std::filesystem::remove(outPath, ec);
        ec.clear();
        std::filesystem::rename(tmpPath, outPath, ec);
        if (ec) {
            result.error = std::string("cannot install embedded helper: ") + ec.message();
            return result;
        }
    }

    result.ok = true;
    result.path = outPath.string();
    return result;
}
