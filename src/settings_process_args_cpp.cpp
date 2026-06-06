#include "settings_process_args_cpp.h"

#include <algorithm>
#include <cctype>

#include "dir_cpp.h"
#include "file_cpp.h"
#include "process_runner.h"
#include "settings_exclusions_cpp.h"

namespace {

static bool ends_with(const std::string &s, const std::string &suffix)
{
    if (suffix.size() > s.size()) {
        return false;
    }
    return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin());
}

static std::uint32_t to_uint_like_qstring_touint(const std::string &s, bool *ok)
{
    // Qt QString::toUInt() ignores leading/trailing whitespace.
    std::size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    std::size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    if (start == end) {
        if (ok) {
            *ok = false;
        }
        return 0;
    }

    std::uint64_t v = 0;
    for (std::size_t i = start; i < end; ++i) {
        const unsigned char ch = static_cast<unsigned char>(s[i]);
        if (ch < '0' || ch > '9') {
            if (ok) {
                *ok = false;
            }
            return 0;
        }
        v = v * 10 + static_cast<std::uint64_t>(ch - '0');
        if (v > 0xFFFFFFFFull) {
            if (ok) {
                *ok = false;
            }
            return 0;
        }
    }

    if (ok) {
        *ok = true;
    }
    return static_cast<std::uint32_t>(v);
}

static void warn(const SettingsProcessArgsCpp::Callbacks &cb, const std::string &msg)
{
    if (cb.onWarning) {
        cb.onWarning(msg);
    }
}

static std::string qt_quote(const std::string &s)
{
    return std::string("\"") + s + "\"";
}

static void critical(const SettingsProcessArgsCpp::Callbacks &cb, const std::string &title, const std::string &msg)
{
    if (cb.onCriticalMessage) {
        cb.onCriticalMessage(title, msg);
    }
}

[[noreturn]] static void die_failure()
{
#ifdef UNIT_TESTS
    throw SettingsProcessArgsCpp::UnitTestExit{EXIT_FAILURE};
#else
    std::exit(EXIT_FAILURE);
#endif
}

static void select_kernel_like_settings_qt(SettingsCpp &settings,
                                          const std::string &currentKernel,
                                          const SettingsProcessArgsCpp::Callbacks &callbacks)
{
    const std::string prefix = "/boot/vmlinuz-";
    if (settings.kernel.rfind(prefix, 0) == 0) {
        settings.kernel.erase(0, prefix.size());
    }

    if (settings.kernel.empty() || !FileCpp::exists("/boot/vmlinuz-" + settings.kernel)) {
        settings.kernel = currentKernel;
        if (!FileCpp::exists("/boot/vmlinuz-" + settings.kernel)) {
            const std::vector<std::string> vmlinuzFiles = DirCpp("/boot").entryList({"vmlinuz-*"}, DirCpp::EntryType::Files, false);
            if (!vmlinuzFiles.empty()) {
                settings.kernel = vmlinuzFiles.back();
                const std::string prefix2 = "vmlinuz-";
                if (settings.kernel.rfind(prefix2, 0) == 0) {
                    settings.kernel.erase(0, prefix2.size());
                }
            }
            if (!FileCpp::exists("/boot/vmlinuz-" + settings.kernel)) {
                critical(callbacks,
                         std::string("Error"),
                         std::string("Could not find a usable kernel\n\n")
                             + "Searched for kernel files in /boot/ but none were found or accessible.");
                die_failure();
            }
        }
    }

    const std::string configPath = std::string("/boot/config-") + settings.kernel;
    const int grepExit = ProcessRunner::execute("grep", {"-q", "^CONFIG_SQUASHFS=[ym]", configPath});
    if (grepExit != 0) {
        critical(callbacks, std::string("Error"), std::string("Current kernel doesn't support Squashfs, cannot continue."));
        die_failure();
    }
}

} // namespace

void SettingsProcessArgsCpp::applyLikeSettingsQt(SettingsCpp &settings, const Input &in, const Callbacks &callbacks)
{
    settings.shutdown = in.shutdownSet;

    if (!in.kernelArg.empty()) {
        settings.kernel = in.kernelArg;
    }

    if (!in.directoryArg.empty()) {
        if (FileCpp::exists(in.directoryArg)) {
            settings.snapshotDir = DirCpp::absolutePath(in.directoryArg) + "/snapshot";
        } else {
            warn(callbacks, std::string("Directory does not exist: ") + qt_quote(in.directoryArg));
            die_failure();
        }
    }

    if (!in.workdirArg.empty()) {
        if (FileCpp::exists(in.workdirArg)) {
            settings.tempDirParent = DirCpp::absolutePath(in.workdirArg);
        } else {
            warn(callbacks, std::string("Work directory does not exist: ") + qt_quote(in.workdirArg));
            die_failure();
        }
    }

    if (!in.dataFilesPathArg.empty()) {
        if (DirCpp::exists(in.dataFilesPathArg + "/files") && DirCpp::exists(in.dataFilesPathArg + "/general-files")) {
            settings.dataFilesPath = DirCpp::absolutePath(in.dataFilesPathArg);
        } else {
            warn(callbacks, std::string("Data files directory is invalid: ") + qt_quote(in.dataFilesPathArg));
            die_failure();
        }
    }

    if (!in.fileArg.empty()) {
        settings.snapshotName = in.fileArg + (ends_with(in.fileArg, ".iso") ? std::string() : std::string(".iso"));
    } else {
        settings.snapshotName = in.defaultSnapshotName;
    }

    if (FileCpp::exists(settings.snapshotDir + "/" + settings.snapshotName)) {
        const std::string message = std::string("Output file ") + settings.snapshotDir + "/" + settings.snapshotName
            + " already exists. Please use another file name, or delete the existent file.";
        critical(callbacks, std::string("Error"), message);
        die_failure();
    }

    settings.resetAccounts = in.resetSet;
    if (settings.resetAccounts) {
        SettingsExclusionsCpp::excludeAllLikeSettingsQt(settings, in.users);
    }

    if (in.monthSet) {
        settings.resetAccounts = true;
    }

    if (in.checksumsSet) {
        settings.makeSha512sum = true;
        settings.makeMd5sum = true;
    }

    if (in.monthSet) {
        settings.makeSha512sum = true;
        settings.makeMd5sum = false;
    }

    if (in.noChecksumsSet) {
        settings.makeSha512sum = false;
        settings.makeMd5sum = false;
    }

    if (!in.compressionArg.empty()) {
        settings.compression = in.compressionArg;
    }

    if (!in.compressionLevelArg.empty()) {
        settings.mksqOpt = in.compressionLevelArg;
    }

    if (!in.coresArg.empty()) {
        bool ok = false;
        const std::uint32_t val = to_uint_like_qstring_touint(in.coresArg, &ok);
        if (!ok || val == 0 || val > settings.maxCores) {
            warn(callbacks, std::string("Invalid cores value: ") + qt_quote(in.coresArg) + " - must be between 1 and "
                + std::to_string(settings.maxCores));
            warn(callbacks, std::string("Using default: ") + std::to_string(settings.cores));
        } else {
            settings.cores = val;
        }
    }

    if (!in.throttleArg.empty()) {
        bool ok = false;
        const std::uint32_t val = to_uint_like_qstring_touint(in.throttleArg, &ok);
        if (!ok || val > 99) {
            warn(callbacks, std::string("Invalid throttle value: ") + qt_quote(in.throttleArg) + " - must be between 0 and 99");
            warn(callbacks, std::string("Using default: ") + std::to_string(settings.throttle));
        } else {
            settings.throttle = val;
        }
    }

    select_kernel_like_settings_qt(settings, in.currentKernel, callbacks);
}
