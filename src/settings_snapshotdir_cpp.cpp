#include "settings_snapshotdir_cpp.h"

#include "command_runner.h"
#include "settings_cpp.h"

#include <vector>

namespace {

static void cb_debug(const SettingsSnapshotDirCpp::Callbacks &cb, const std::string &text)
{
    if (cb.debug) {
        cb.debug(text);
    }
}

} // namespace

bool SettingsSnapshotDirCpp::checkSnapshotDirLikeSettingsQt(const SettingsCpp &s, const SettingsSnapshotDirCpp::Callbacks &cb)
{
    cb_debug(cb, std::string("+++ bool Settings::checkSnapshotDir() const +++"));

    const CommandRunner::Result mk = CommandRunner::procAsRoot(
        "mkdir", std::vector<std::string>{"-p", s.snapshotDir}, std::string(), CommandRunner::QuietMode::No);

    if (!(mk.started && mk.normalExit && mk.exitCode == 0)) {
        cb_debug(cb, std::string("Could not create work directory. ") + s.snapshotDir + "\n");
        return false;
    }

    const std::string userName = CommandRunner::loggedInUserName();
    if (!userName.empty()) {
        (void)CommandRunner::procAsRoot("chown",
                                       std::vector<std::string>{userName + ":", s.snapshotDir},
                                       std::string(),
                                       CommandRunner::QuietMode::Yes);
    }

    return true;
}
