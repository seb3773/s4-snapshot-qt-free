#include "settings_space_cpp.h"

#include <cstdio>
#include <iomanip>
#include <locale>
#include <sstream>

#include "dir_cpp.h"
#include "filesystemutils_cpp.h"
#include "file_cpp.h"
#include "string_cpp.h"
#include "qsettings_cpp.h"
#include "command_runner.h"
#include "settings_liverootspace_cpp.h"

namespace {

static std::string format_double_fixed_2(double v)
{
    // Match Qt QString::number(x, 'f', 2): always '.' decimal separator.
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << std::fixed << std::setprecision(2) << v;
    return oss.str();
}

static std::string format_kib_error(const std::string &prefix,
                                    std::uint64_t availableSpace,
                                    std::uint64_t minFreeSpace)
{
    // Match Settings::validateSpaceRequirements error formatting (GUI build, untranslated source string).
    // Qt: "Insufficient free space: %1 KiB available, minimum %2 KiB required"
    return prefix + std::to_string(availableSpace)
        + " KiB available, minimum " + std::to_string(minFreeSpace) + " KiB required";
}

static void cb_debug(const SettingsSpaceCpp::Callbacks &cb, const std::string &text)
{
    if (cb.debug) {
        cb.debug(text);
    }
}

static void cb_critical(const SettingsSpaceCpp::Callbacks &cb, const std::string &text)
{
    if (cb.critical) {
        cb.critical(text);
    }
}

static std::string format_double_fixed_2_gib(double bytes)
{
    constexpr double factor = 1024.0 * 1024.0 * 1024.0;
    return format_double_fixed_2(bytes / factor) + "GiB";
}

static std::string format_kib_as_mib(std::uint64_t kib)
{
    constexpr double kibToMib = 1024.0;
    return format_double_fixed_2(static_cast<double>(kib) / kibToMib) + "MiB";
}

static std::uint64_t get_live_root_space_like_settings_qt()
{
    SettingsLiveRootSpaceCpp::Callbacks cb;
    cb.warning = [](const std::string &t) { std::fputs(t.c_str(), stderr); };
    return SettingsLiveRootSpaceCpp::getLiveRootSpaceLikeSettingsQt(cb);
}

} // namespace

std::string SettingsSpaceCpp::getUsedSpaceLikeSettingsQt(const SettingsCpp &settings)
{
    std::string out = "\n- Used space on / (root): ";

    std::uint64_t rootSizeBytes = 0;
    if (settings.live) {
        rootSizeBytes = get_live_root_space_like_settings_qt();
        out += format_double_fixed_2_gib(static_cast<double>(rootSizeBytes));
        out += " -- estimated";
    } else {
        rootSizeBytes = static_cast<std::uint64_t>(FileSystemUtilsCpp::bytesTotal(std::string("/")))
                        - static_cast<std::uint64_t>(FileSystemUtilsCpp::bytesFree(std::string("/")));
        out += format_double_fixed_2_gib(static_cast<double>(rootSizeBytes));
    }

    const bool isHomeMount = FileSystemUtilsCpp::isMountPoint(std::string("/home/"));
    if (isHomeMount) {
        const std::uint64_t homeSizeBytes
            = static_cast<std::uint64_t>(FileSystemUtilsCpp::bytesTotal(std::string("/home/")))
              - static_cast<std::uint64_t>(FileSystemUtilsCpp::bytesFree(std::string("/home/")));
        out += "\n- Used space on /home: ";
        out += format_double_fixed_2_gib(static_cast<double>(homeSizeBytes));
    }

    return out;
}

bool SettingsSpaceCpp::validateSpaceRequirementsLikeSettingsQt(const SettingsCpp &settings,
                                                               const Callbacks &cb)
{
    // Port of Settings::validateSpaceRequirements() (Qt-free).
    cb_debug(cb, std::string("+++ bool Settings::validateSpaceRequirements() const +++"));
    constexpr std::uint64_t MIN_FREE_SPACE = 1024ull * 1024ull; // 1GB in KiB

    std::string pathToCheck = settings.snapshotDir;
    if (!DirCpp::exists(settings.snapshotDir)) {
        pathToCheck = DirCpp::absolutePathOfContainingDir(settings.snapshotDir);
    }

    const std::uint64_t availableSpace = FileSystemUtilsCpp::getFreeSpaceKiB(pathToCheck);
    if (availableSpace < MIN_FREE_SPACE) {
        cb_critical(cb, format_kib_error("Insufficient free space: ", availableSpace, MIN_FREE_SPACE));
        return false;
    }

    if (settings.freeSpaceWork > 0 && settings.freeSpaceWork < MIN_FREE_SPACE) {
        cb_critical(cb,
                    format_kib_error("Insufficient free space in work directory: ",
                                     settings.freeSpaceWork,
                                     MIN_FREE_SPACE)
                        );
        return false;
    }

    cb_debug(cb, std::string("Space requirements validation passed"));
    return true;
}

int SettingsSpaceCpp::getSnapshotCountLikeSettingsQt(const SettingsCpp &settings)
{
    if (FileCpp::exists(settings.snapshotDir)) {
        const std::vector<std::string> list = DirCpp(settings.snapshotDir).entryInfoList({"*.iso"}, DirCpp::EntryType::Files);
        return static_cast<int>(list.size());
    }
    return 0;
}

std::string SettingsSpaceCpp::getSnapshotSizeLikeSettingsQt(const SettingsCpp &settings)
{
    std::int64_t totalSize = 0;
    if (FileCpp::exists(settings.snapshotDir)) {
        const std::vector<std::string> isoFiles = DirCpp(settings.snapshotDir).entryList({"*.iso"}, DirCpp::EntryType::Files);
        for (const std::string &file : isoFiles) {
            const std::string filePath = DirCpp(settings.snapshotDir).filePath(file);
            totalSize += FileCpp::size(filePath);
        }
    }

    const std::int64_t mib = totalSize / (1024 * 1024);
    return std::to_string(mib) + "MiB";
}

std::string SettingsSpaceCpp::getFreeSpaceStringsLikeSettingsQt(SettingsCpp &settings,
                                                                const std::string &path,
                                                                const Callbacks &cb)
{
    constexpr float factor = 1024 * 1024;

    settings.freeSpace = static_cast<std::uint64_t>(FileSystemUtilsCpp::getFreeSpaceKiB(path));

    const std::string out = format_double_fixed_2(static_cast<double>(settings.freeSpace) / static_cast<double>(factor)) + "GiB";

    cb_debug(cb,
             std::string("- Free space on ") + path + ", where snapshot folder is placed: " + out + " \n\n");

    const int snapCount = getSnapshotCountLikeSettingsQt(settings);
    const std::string snapSize = getSnapshotSizeLikeSettingsQt(settings);

    cb_debug(cb,
             std::string(
                 "The free space should be sufficient to hold the compressed data from / and /home\n\n"
                 "      If necessary, you can create more available space\n"
                 "      by removing previous snapshots and saved copies:\n"
                 "      ")
                 + std::to_string(snapCount) + " snapshots are taking up " + snapSize + " of disk space.\n\n");

    return out;
}

std::string SettingsSpaceCpp::formatRequiredSpaceEstimateDebugLikeSettingsQt(
    const WorkSpaceCpp::RequiredSpaceEstimate &estimate,
    const std::uint64_t freeSpaceKiB)
{
    std::string out;
    out += "SIZE         " + format_kib_as_mib(estimate.rootKiB) + "\n";
    out += "SIZE EXCLUDES " + format_kib_as_mib(estimate.excludesKiB) + "\n";
    out += "COMPRESSION  " + std::to_string(estimate.compressionPercent) + "\n";
    out += "SIZE NEEDED  " + format_kib_as_mib(estimate.requiredKiB) + "\n";
    out += "SIZE FREE    " + format_kib_as_mib(freeSpaceKiB) + "\n";
    return out;
}

WorkSpaceCpp::RequiredSpaceEstimate SettingsSpaceCpp::getRequiredSpaceEstimateLikeSettingsQt(
    const SettingsCpp &settings,
    const std::string &applicationName,
    const Callbacks &cb)
{
    WorkSpaceCpp::Callbacks wcb;
    wcb.message = [&](const std::string &text) { cb_debug(cb, text); };
    wcb.warning = [&](const std::string &text) { cb_critical(cb, text); };
    return WorkSpaceCpp::getRequiredSpaceEstimateLikeQt(settings, applicationName, wcb);
}
