#include "settings_liverootspace_cpp.h"

#include "command_runner.h"
#include "file_cpp.h"
#include "filesystemutils_cpp.h"
#include "qsettings_cpp.h"
#include "string_cpp.h"

#include <cstdio>

namespace {

static void cb_warning(const SettingsLiveRootSpaceCpp::Callbacks &cb, const std::string &text)
{
    if (cb.warning) {
        cb.warning(text);
    }
}

static std::string linuxfs_compression_name_from_type_code_like_settings_qt(const std::string &code)
{
    if (code == "1") return "gzip";
    if (code == "2") return "lzo";
    if (code == "3") return "lzma";
    if (code == "4") return "xz";
    if (code == "5") return "lz4";
    if (code == "6") return "zstd";
    return std::string();
}

static std::uint8_t compression_factor_value_like_settings_qt(const std::string &compressionName)
{
    if (compressionName == "xz") return 31;
    if (compressionName == "zstd") return 35;
    if (compressionName == "gzip") return 37;
    if (compressionName == "lzo") return 52;
    if (compressionName == "lzma") return 52;
    if (compressionName == "lz4") return 52;
    return 0;
}

} // namespace

std::uint64_t SettingsLiveRootSpaceCpp::getLiveRootSpaceLikeSettingsQt(const SettingsLiveRootSpaceCpp::Callbacks &cb)
{
    const std::string initrdOut = "/live/config/initrd.out";

    std::string sqfile_full = QSettingsCpp::nativeGeneralValueString(initrdOut, "SQFILE_FULL", "/live/boot-dev/antiX/linuxfs");
    const std::string toram_mp = QSettingsCpp::nativeGeneralValueString(initrdOut, "TORAM_MP", "/live/to-ram");
    std::string sqfile_path = QSettingsCpp::nativeGeneralValueString(initrdOut, "SQFILE_PATH", "antiX");
    while (!sqfile_path.empty() && sqfile_path.front() == '/') {
        sqfile_path.erase(sqfile_path.begin());
    }
    const std::string sqfile_name = QSettingsCpp::nativeGeneralValueString(initrdOut, "SQFILE_NAME", "linuxfs");

    if (!toram_mp.empty()) {
        const std::string toramCandidate = toram_mp + "/" + sqfile_path + "/" + sqfile_name;
        if (FileCpp::exists(toramCandidate)) {
            sqfile_full = toramCandidate;
        }
    }

    const std::string ddCmd = std::string("dd if=") + sqfile_full
        + " bs=1 skip=20 count=2 status=none 2>/dev/null |od -An -tdI";
    const std::string linuxfs_compression_type = CommandRunner::getOut(ddCmd, CommandRunner::QuietMode::Yes);

    constexpr std::uint8_t default_factor = 30;
    std::uint8_t c_factor = default_factor;

    const std::string compName = linuxfs_compression_name_from_type_code_like_settings_qt(
        StringCpp::trimmedLikeQStringUtf8(linuxfs_compression_type));
    if (!compName.empty()) {
        const std::uint8_t v = compression_factor_value_like_settings_qt(compName);
        if (v != 0) {
            c_factor = v;
        }
    } else {
        const std::string t = StringCpp::trimmedLikeQStringUtf8(linuxfs_compression_type);
        cb_warning(cb, std::string("Unknown compression type: ") + t + "\n");
    }

    std::uint64_t rootfs_file_size = 0;
    const std::uint64_t linuxfs_file_size = static_cast<std::uint64_t>(FileSystemUtilsCpp::bytesTotal("/live/linux/"))
        * 100ULL / static_cast<std::uint64_t>(c_factor);

    if (FileCpp::exists("/live/persist-root")) {
        rootfs_file_size = static_cast<std::uint64_t>(FileSystemUtilsCpp::bytesTotal("/live/persist-root/"));
    }

    return linuxfs_file_size + rootfs_file_size;
}
