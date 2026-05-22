#include "settings_tempdir_cpp.h"

#include "dir_cpp.h"
#include "file_cpp.h"
#include "filesystemutils_cpp.h"
#include "string_cpp.h"

#include <cstdlib>

namespace {

static void cb_debug(const SettingsTempDirCpp::Callbacks &cb, const std::string &text)
{
    if (cb.debug) {
        cb.debug(text);
    }
}

static void cb_critical(const SettingsTempDirCpp::Callbacks &cb, const std::string &text)
{
    if (cb.critical) {
        cb.critical(text);
    }
}

static std::string trim_ascii_ws_like_trimmedLikeQt(const std::string &s)
{
    return StringCpp::trimmedLikeQStringUtf8(s);
}

} // namespace

bool SettingsTempDirCpp::checkTempDirLikeSettingsQt(SettingsCpp &s,
                                                   const SettingsTempDirCpp::Callbacks &cb,
                                                   TempDir *tmpOut)
{
    cb_debug(cb, std::string("+++ bool Settings::checkTempDir() +++\n"));

    auto exists_and_supported = [](const std::string &p) {
        return !p.empty() && FileCpp::exists(p) && FileSystemUtilsCpp::isOnSupportedPartition(p);
    };

    if (!exists_and_supported(s.tempDirParent)) {
        const bool snapSupported = FileSystemUtilsCpp::isOnSupportedPartition(s.snapshotDir);
        s.tempDirParent = snapSupported ? FileSystemUtilsCpp::largerFreeSpace("/tmp", "/home", s.snapshotDir)
                                        : FileSystemUtilsCpp::largerFreeSpace("/tmp", "/home");
    }

    if (s.tempDirParent == "/home") {
        std::string userName;
        if (const char *v = std::getenv("SUDO_USER")) {
            userName = trim_ascii_ws_like_trimmedLikeQt(std::string(v));
        }
        if (userName.empty()) {
            if (const char *v = std::getenv("LOGNAME")) {
                userName = trim_ascii_ws_like_trimmedLikeQt(std::string(v));
            }
        }
        s.tempDirParent = std::string("/home/") + userName;
    }

    TempDir tmp(s.tempDirParent + "/s4-snapshot-XXXXXXXX");
    if (!tmp.isValid()) {
        // Match Settings::checkTempDir() output: 3 qCritical lines.
        // NOTE: Settings::checkTempDir() prints tmpdir->path() even on failure.
        cb_critical(cb, std::string("Could not create temp directory: ") + tmp.path() + "\n");
        cb_critical(cb, std::string("Please check that the parent directory exists and is writable:\n"));
        cb_critical(cb, s.tempDirParent + "\n");
        return false;
    }

    s.workDir = tmp.path();
    s.freeSpaceWork = FileSystemUtilsCpp::getFreeSpaceKiB(s.workDir);
    (void)DirCpp().mkpath(s.workDir + "/iso-template/antiX");

    cb_debug(cb, std::string("Work directory is placed in ") + s.tempDirParent + "\n");

    if (tmpOut) {
        *tmpOut = std::move(tmp);
    }
    return true;
}
