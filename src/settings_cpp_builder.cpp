#include "settings_cpp_builder.h"

#include <cerrno>
#include <cstdlib>
#include <limits>
#include <map>
#include <vector>
#include <unistd.h>

#include "command_runner.h"
#include "config_paths.h"
#include "dir_cpp.h"
#include "file_cpp.h"
#include "filesystemutils_cpp.h"
#include "qsettings_cpp.h"
#include "string_cpp.h"
#include "systeminfo_cpp.h"

namespace {

#ifdef UNIT_TESTS
static std::string g_ut_excludes_source_path_override;
static std::string g_ut_fallback_excludes_path_override;
#endif

static std::string trim_quotes_like_settings_cpp(const std::string &utf8)
{
    // Mirror Settings::trimQuotes logic: trimmed first, then strip matching single/double quotes.
    std::string t = StringCpp::trimmedLikeQStringUtf8(utf8);
    if (t.size() >= 2 && t.front() == '\'' && t.back() == '\'') {
        t = StringCpp::removeLikeQStringUtf8(t, 0, 1);
        t = StringCpp::removeLikeQStringUtf8(t, static_cast<int>(t.size()) - 1, 1);
    }
    if (t.size() >= 2 && t.front() == '"' && t.back() == '"') {
        t = StringCpp::removeLikeQStringUtf8(t, 0, 1);
        t = StringCpp::removeLikeQStringUtf8(t, static_cast<int>(t.size()) - 1, 1);
    }
    return t;
}

static std::string clean_path_std(const std::string &p)
{
    return DirCpp::cleanPath(p);
}

static std::uint32_t to_uint32_like_qstring_toUInt_base10(const std::string &utf8)
{
    const std::string t = StringCpp::trimmedLikeQStringUtf8(utf8);
    if (t.empty()) {
        return 0;
    }
    if (!t.empty() && t[0] == '-') {
        return 0;
    }

    errno = 0;
    char *end = nullptr;
    const unsigned long long v = std::strtoull(t.c_str(), &end, 10);
    if (end == t.c_str() || (end != nullptr && *end != '\0')) {
        return 0;
    }
    if (errno == ERANGE || v > static_cast<unsigned long long>(std::numeric_limits<std::uint32_t>::max())) {
        return 0;
    }
    return static_cast<std::uint32_t>(v);
}

static std::string logged_in_user_name_std()
{
    return CommandRunner::loggedInUserName();
}

static void chown_file_to_logged_in_user_std(const std::string &path)
{
    const std::string username = logged_in_user_name_std();
    if (username.empty() || path.empty()) {
        return;
    }

    // Mirror Settings::chownFileToLoggedInUser: return if file missing or already writable.
    if (!FileCpp::exists(path) || ::access(path.c_str(), W_OK) == 0) {
        return;
    }
    (void)CommandRunner::procAsRoot("chown",
                                   std::vector<std::string>{username + ":", path},
                                   std::string(),
                                   CommandRunner::QuietMode::Yes);
}

static std::vector<std::string> build_executable_search_path_std()
{
    const char *env = std::getenv("PATH");
    const std::string path_env = (env != nullptr ? std::string(env) : std::string());
    std::vector<std::string> out = StringCpp::splitLikeQString(path_env, ":", StringCpp::SplitBehavior::KeepEmptyParts);
    out.push_back("/usr/sbin");
    return out;
}

} // namespace

#ifdef UNIT_TESTS
void SettingsCppBuilder::ut_setExcludesSourcePathOverride(const std::string &path)
{
    g_ut_excludes_source_path_override = path;
}

void SettingsCppBuilder::ut_setFallbackExcludesPathOverride(const std::string &path)
{
    g_ut_fallback_excludes_path_override = path;
}
#endif

SettingsCpp SettingsCppBuilder::buildFromArgs(const SettingsArgsCpp &args,
                                             bool isGuiApp,
                                             const std::string &appName,
                                             const std::string &organizationName)
{
    const std::string config_dir = S4SnapshotConfig::userConfigBaseDirStd();
    const std::string defaults_path = S4SnapshotConfig::bundledConfigPath(appName);
    return buildFromArgsWithPaths(args, isGuiApp, appName, organizationName, defaults_path, config_dir);
}

SettingsCpp SettingsCppBuilder::buildFromArgsWithPaths(const SettingsArgsCpp &args,
                                                      bool isGuiApp,
                                                      const std::string &appName,
                                                      const std::string &organizationName,
                                                      const std::string &defaultsConfigPath,
                                                      const std::string &userConfigBaseDir)
{
    SettingsCpp out;

    out.x86 = SystemInfoCpp::is386();
    {
        if (args.maxCoresOverride != 0) {
            out.maxCores = args.maxCoresOverride;
        } else {
            const std::string nproc_out = CommandRunner::getOut("nproc", CommandRunner::QuietMode::Yes);
            const std::string trimmed = StringCpp::trimmedLikeQStringUtf8(nproc_out);
            out.maxCores = static_cast<std::uint32_t>(std::strtoul(trimmed.c_str(), nullptr, 10));
        }
    }

    out.monthly = args.monthly;
    out.overrideSize = args.overrideSize;

    S4SnapshotConfig::ensureUserConfigFile(userConfigBaseDir, organizationName, appName, defaultsConfigPath);

    out.workDir.clear();
    out.snapshotDir.clear();
    out.snapshotName.clear();
    out.tempDirParent.clear();
    out.dataFilesPath.clear();
    out.runtimeScriptsPath.clear();
    out.shutdown = false;

    out.makeIsohybrid = false;
    out.makeMd5sum = false;
    out.makeSha512sum = false;

    out.kernel.clear();
    out.compression.clear();
    out.cores = 0;
    out.throttle = 0;
    out.mksqOpt.clear();
    out.bootOptions.clear();

    const std::string config_dir = userConfigBaseDir;
    const std::string user_primary_path = QSettingsCpp::nativeUserPrimaryFilePathFromBaseDir(config_dir, organizationName, appName);

    const auto user_value = [&](const std::string &key, const std::string &def) -> std::string {
        return QSettingsCpp::nativeUserValueStringFromBaseDir(config_dir, organizationName, appName, key, def);
    };
    const auto user_set = [&](const std::string &key, const std::string &value) -> void {
        (void)QSettingsCpp::nativeGeneralSetValueString(user_primary_path, key, value);
    };

    out.editBootMenu = (user_value("edit_boot_menu", "no") != "no");

    out.sessionExcludes.clear();

    out.snapshotDir = trim_quotes_like_settings_cpp(user_value("snapshot_dir", "/home/snapshot"));
    if (!StringCpp::endsWithLikeQStringUtf8(out.snapshotDir, "/snapshot")) {
        out.snapshotDir = clean_path_std(out.snapshotDir + "/snapshot");
    }

    out.guiEditor = trim_quotes_like_settings_cpp(user_value("gui_editor", ""));

    const std::string user_config_dir = S4SnapshotConfig::userConfigDirFromBase(config_dir, organizationName);
    const std::string user_excludes_path = S4SnapshotConfig::userExcludesPathFromBase(config_dir, organizationName, appName);

    const std::string bundled_excludes_path = S4SnapshotConfig::bundledExcludesPath(appName);
    const std::string legacy_bundled_excludes_path = S4SnapshotConfig::legacyBundledExcludesPath(appName);
    const std::string local_path = clean_path_std(std::string("/usr/local/share/excludes/") + appName + "-exclude.list");
#ifdef UNIT_TESTS
    const std::string fallback_excludes_path = !g_ut_fallback_excludes_path_override.empty()
        ? g_ut_fallback_excludes_path_override
        : (FileCpp::exists(local_path) ? local_path : legacy_bundled_excludes_path);
    out.excludesSourcePath = !g_ut_excludes_source_path_override.empty()
        ? g_ut_excludes_source_path_override
        : (FileCpp::exists(bundled_excludes_path) ? bundled_excludes_path : fallback_excludes_path);
#else
    const std::string fallback_excludes_path = FileCpp::exists(local_path) ? local_path : legacy_bundled_excludes_path;
    out.excludesSourcePath = FileCpp::exists(bundled_excludes_path) ? bundled_excludes_path : fallback_excludes_path;
#endif

    S4SnapshotConfig::ensureUserExcludesFile(config_dir, organizationName, appName, out.excludesSourcePath);

    const std::string configured_excludes_path =
        trim_quotes_like_settings_cpp(user_value("snapshot_excludes", user_excludes_path));
    const std::string canonical_excludes_path = S4SnapshotConfig::normalizeUserExcludesToCanonical(
        config_dir, organizationName, appName, configured_excludes_path);
    user_set("snapshot_excludes", canonical_excludes_path);
    if (!FileCpp::exists(canonical_excludes_path)) {
        (void)FileCpp::copy(fallback_excludes_path, canonical_excludes_path);
    }
    out.snapshotExcludesPath = canonical_excludes_path;

    chown_file_to_logged_in_user_std(user_primary_path);
    chown_file_to_logged_in_user_std(canonical_excludes_path);

    out.makeMd5sum = (user_value("make_md5sum", "no") != "no");
    out.makeSha512sum = (user_value("make_sha512sum", "no") != "no");
    out.compression = trim_quotes_like_settings_cpp(user_value("compression", "zstd"));
    out.mksqOpt = trim_quotes_like_settings_cpp(user_value("mksq_opt", ""));
    out.tempDirParent = trim_quotes_like_settings_cpp(user_value("workdir", ""));

    {
        const std::string cores_default = std::to_string(out.maxCores);
        const std::string cores_value = user_value("cores", cores_default);
        const std::uint32_t stored = static_cast<std::uint32_t>(std::strtoul(StringCpp::trimmedLikeQStringUtf8(cores_value).c_str(), nullptr, 10));
        out.cores = (stored == 0 || stored > out.maxCores) ? out.maxCores : stored;
        if (out.cores != stored) {
            user_set("cores", std::to_string(out.cores));
        }
    }

    {
        const std::string throttle_value = user_value("throttle", "0");
        out.throttle = to_uint32_like_qstring_toUInt_base10(throttle_value);
    }

    if (!args.fileArg.empty()) {
        out.snapshotName = args.fileArg;
        if (!StringCpp::endsWithLikeQStringUtf8(out.snapshotName, ".iso")) {
            out.snapshotName += ".iso";
        }
    } else {
        out.snapshotName = "snapshot1.iso";
    }

    (void)isGuiApp;
    (void)build_executable_search_path_std();
    (void)args.preempt;

    return out;
}

SettingsCpp SettingsCppBuilder::build(const CommandLineParserStd &argParser,
                                     bool isGuiApp,
                                     const std::string &appName,
                                     const std::string &organizationName)
{
    SettingsArgsCpp args;
    args.monthly = argParser.isSet("month");
    args.overrideSize = argParser.isSet("override-size");
    args.preempt = argParser.isSet("preempt");
    args.fileArg = argParser.value("file");
    return buildFromArgs(args, isGuiApp, appName, organizationName);
}
