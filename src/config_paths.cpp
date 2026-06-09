#include "config_paths.h"

#include <map>
#include <unistd.h>
#include <vector>

#include "command_runner.h"
#include "dir_cpp.h"
#include "file_cpp.h"
#include "qsettings_cpp.h"
#include "standard_paths_cpp.h"

namespace S4SnapshotConfig {

static void chown_file_to_logged_in_user_std(const std::string &path)
{
    const std::string username = CommandRunner::loggedInUserName();
    if (username.empty() || path.empty()) {
        return;
    }
    if (!FileCpp::exists(path) || ::access(path.c_str(), W_OK) == 0) {
        return;
    }
    (void)CommandRunner::procAsRoot("chown",
                                   std::vector<std::string>{username + ":", path},
                                   std::string(),
                                   CommandRunner::QuietMode::Yes);
}

std::string userConfigBaseDirStd()
{
    const std::string username = CommandRunner::loggedInUserName();
    if (!username.empty()) {
        const std::string candidate_home = DirCpp::cleanPath(std::string("/home/") + username);
        if (DirCpp::exists(candidate_home)) {
            return DirCpp::cleanPath(candidate_home + "/.config");
        }
    }
    return StandardPathsCpp::writableConfigLocation();
}

std::string bundledConfigPath(const std::string &appName)
{
    return std::string(kShareDir) + "/" + appName + ".conf";
}

std::string bundledExcludesPath(const std::string &appName)
{
    return std::string(kShareDir) + "/" + appName + "-exclude.list";
}

std::string legacyBundledExcludesPath(const std::string &appName)
{
    return std::string("/usr/share/excludes/") + appName + "-exclude.list";
}

std::string userConfigDirFromBase(const std::string &configBaseDir, const std::string &organizationName)
{
    return DirCpp::cleanPath(configBaseDir + "/" + organizationName);
}

std::string userExcludesPathFromBase(const std::string &configBaseDir,
                                     const std::string &organizationName,
                                     const std::string &appName)
{
    return DirCpp::cleanPath(userConfigDirFromBase(configBaseDir, organizationName) + "/" + appName + "-exclude.list");
}

static void mergeMissingKeysFromDefaults(const std::string &userPrimaryPath, const std::string &defaultsConfigPath)
{
    if (!FileCpp::exists(defaultsConfigPath)) {
        return;
    }

    const std::map<std::string, std::string> user_kv = QSettingsCpp::nativeGeneralAllKeyValues(userPrimaryPath);
    const std::map<std::string, std::string> defaults_kv = QSettingsCpp::nativeGeneralAllKeyValues(defaultsConfigPath);
    for (const auto &kv : defaults_kv) {
        if (user_kv.find(kv.first) == user_kv.end()) {
            (void)QSettingsCpp::nativeGeneralSetValueString(userPrimaryPath, kv.first, kv.second);
        }
    }
}

void ensureUserConfigFile(const std::string &configBaseDir,
                          const std::string &organizationName,
                          const std::string &appName,
                          const std::string &defaultsConfigPath)
{
    const std::string user_primary_path =
        QSettingsCpp::nativeUserPrimaryFilePathFromBaseDir(configBaseDir, organizationName, appName);
    if (FileCpp::exists(user_primary_path)) {
        mergeMissingKeysFromDefaults(user_primary_path, defaultsConfigPath);
        return;
    }

    const std::string user_config_dir = userConfigDirFromBase(configBaseDir, organizationName);
    (void)DirCpp().mkpath(user_config_dir);

    const std::string legacy_primary_path =
        QSettingsCpp::nativeUserPrimaryFilePathFromBaseDir(configBaseDir, kLegacyOrganizationName, appName);
    if (FileCpp::exists(legacy_primary_path)) {
        (void)FileCpp::copy(legacy_primary_path, user_primary_path);
    } else if (FileCpp::exists(defaultsConfigPath)) {
        (void)FileCpp::copy(defaultsConfigPath, user_primary_path);
    }

    mergeMissingKeysFromDefaults(user_primary_path, defaultsConfigPath);
}

void ensureUserExcludesFile(const std::string &configBaseDir,
                            const std::string &organizationName,
                            const std::string &appName,
                            const std::string &defaultsExcludesPath)
{
    const std::string user_excludes_path = userExcludesPathFromBase(configBaseDir, organizationName, appName);
    if (FileCpp::exists(user_excludes_path)) {
        return;
    }

    const std::string user_config_dir = userConfigDirFromBase(configBaseDir, organizationName);
    (void)DirCpp().mkpath(user_config_dir);

    const std::string legacy_excludes_path =
        userExcludesPathFromBase(configBaseDir, kLegacyOrganizationName, appName);
    if (FileCpp::exists(legacy_excludes_path)) {
        (void)FileCpp::copy(legacy_excludes_path, user_excludes_path);
        return;
    }

    if (!defaultsExcludesPath.empty() && FileCpp::exists(defaultsExcludesPath)) {
        (void)FileCpp::copy(defaultsExcludesPath, user_excludes_path);
    }
}

std::string normalizeUserExcludesToCanonical(const std::string &configBaseDir,
                                             const std::string &organizationName,
                                             const std::string &appName,
                                             const std::string &configuredExcludesPath)
{
    const std::string canonical_path = userExcludesPathFromBase(configBaseDir, organizationName, appName);
    const std::string configured_clean = DirCpp::cleanPath(configuredExcludesPath);

    if (!configured_clean.empty() && configured_clean != canonical_path && FileCpp::exists(configured_clean)) {
        (void)DirCpp().mkpath(userConfigDirFromBase(configBaseDir, organizationName));
        (void)FileCpp::copy(configured_clean, canonical_path);
    }

    return canonical_path;
}

void persistSnapshotDir(const std::string &configBaseDir,
                        const std::string &organizationName,
                        const std::string &appName,
                        const std::string &snapshotDir)
{
    if (snapshotDir.empty()) {
        return;
    }

    const std::string user_primary_path =
        QSettingsCpp::nativeUserPrimaryFilePathFromBaseDir(configBaseDir, organizationName, appName);
    (void)DirCpp().mkpath(userConfigDirFromBase(configBaseDir, organizationName));
    (void)QSettingsCpp::nativeGeneralSetValueString(user_primary_path, "snapshot_dir", snapshotDir);
    chown_file_to_logged_in_user_std(user_primary_path);
}

} // namespace S4SnapshotConfig
