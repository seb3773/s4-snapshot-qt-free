#include "app_translator_cpp.h"
#include "batchprocessing_cpp.h"
#include "batchprocessing_cpp_runner.h"
#include "cli_parser_kv_gen.h"
#include "command_line_parser_std.h"
#include "command_runner.h"
#include "dir_cpp.h"
#include "log.h"
#include "logger_cpp.h"
#include "messagehandler_cpp.h"
#include "process_runner.h"
#include "settings_cpp_builder.h"
#include "settings_filename_cpp.h"
#include "settings_space_cpp.h"
#include "settings_checkconfiguration_cpp.h"
#include "settings_initialization_error_cpp.h"
#include "settings_process_args_cpp.h"
#include "settings_validation_cpp.h"
#include "stdio_cpp.h"
#include "string_cpp.h"
#include "systeminfo_cpp.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <unistd.h>

namespace {

static std::string formatPercent1(std::string s, const std::string &arg1)
{
    std::string::size_type pos = 0;
    while ((pos = s.find("%1", pos)) != std::string::npos) {
        s.replace(pos, 2, arg1);
        pos += arg1.size();
    }
    return s;
}

static std::string locale_name_like_qlocale_name()
{
    const char *lang = std::getenv("LANG");
    if (lang == nullptr || *lang == '\0') {
        return std::string();
    }

    std::string s(lang);
    const std::size_t dotPos = s.find('.');
    if (dotPos != std::string::npos) {
        s = s.substr(0, dotPos);
    }
    const std::size_t atPos = s.find('@');
    if (atPos != std::string::npos) {
        s = s.substr(0, atPos);
    }
    return s;
}

static std::string basename_like_qfileinfo_filename(const char *path)
{
    if (path == nullptr) {
        return std::string();
    }
    const char *slash = std::strrchr(path, '/');
    return std::string((slash != nullptr) ? (slash + 1) : path);
}

static bool arg_present(int argc, char **argv, const char *needle)
{
    for (int i = 1; i < argc; ++i) {
        if (argv[i] != nullptr && std::strcmp(argv[i], needle) == 0) {
            return true;
        }
    }
    return false;
}

static bool is_allowed_compression(const std::string &v)
{
    return v == "" || v == "lz4" || v == "lzo" || v == "gzip" || v == "xz" || v == "zstd";
}

static SettingsProcessArgsCpp::Input make_process_args_input(const CommandLineParserStd &parser,
                                                            const std::string &currentKernel,
                                                            const std::vector<std::string> &users,
                                                            const std::string &defaultSnapshotName)
{
    SettingsProcessArgsCpp::Input in;

    in.currentKernel = currentKernel;
    in.users = users;

    in.shutdownSet = parser.isSet("shutdown");
    in.resetSet = parser.isSet("reset");
    in.monthSet = parser.isSet("month");
    in.checksumsSet = parser.isSet("checksums");
    in.noChecksumsSet = parser.isSet("no-checksums");

    in.kernelArg = parser.value("kernel");
    in.directoryArg = parser.value("directory");
    in.workdirArg = parser.value("workdir");
    in.fileArg = parser.value("file");
    in.compressionArg = parser.value("compression");
    in.compressionLevelArg = parser.value("compression-level");
    in.coresArg = parser.value("cores");
    in.throttleArg = parser.value("throttle");
    in.dataFilesPathArg = parser.value("datafiles-path");

    in.defaultSnapshotName = defaultSnapshotName;

    return in;
}

static std::string get_current_kernel_std()
{
    const ProcessRunner::Result r = ProcessRunner::run("uname", { "-r" });
    if (!r.started || r.exitStatus != ProcessRunner::ExitStatus::NormalExit || r.exitCode != 0) {
        return std::string();
    }
    return StringCpp::trimmedLikeQStringUtf8(r.stdoutText);
}

static std::string format_args_like_qt_qdebug_qstringlist(const std::vector<std::string> &argsStd)
{
    std::string out;
    out += "Args: (";
    for (std::size_t i = 0; i < argsStd.size(); ++i) {
        if (i != 0) {
            out += ", ";
        }
        out += "\"";
        out += argsStd[i];
        out += "\"";
    }
    out += ")";
    return out;
}

static int fail_configuration_validation_like_settings_qt()
{
    SettingsInitializationErrorCpp::handleInitializationErrorLikeSettingsQt("iso-snapshot-cli",
                                                                            "Configuration validation failed");
    return EXIT_FAILURE;
}

static int fail_initialize_configuration_like_settings_qt(const std::string &error)
{
    SettingsInitializationErrorCpp::handleInitializationErrorLikeSettingsQt("iso-snapshot-cli", error);
    return EXIT_FAILURE;
}

static bool file_readable_like_qt(const std::string &path)
{
    FileCpp f(path);
    if (!f.open(FileCpp::OpenMode::ReadOnly)) {
        return false;
    }
    f.close();
    return true;
}

static bool initialize_configuration_like_settings_qt()
{
    LoggerCpp::log(LoggerCpp::Level::Debug, "+++ bool Settings::initializeConfiguration() +++");

    const std::string configPath = "/etc/iso-snapshot-cli.conf";
    if (!FileCpp::exists(configPath)) {
        LoggerCpp::log(LoggerCpp::Level::Warning, "Configuration file does not exist: " + configPath);
        LoggerCpp::log(LoggerCpp::Level::Warning, "Using default settings");
    } else {
        if (!file_readable_like_qt(configPath)) {
            LoggerCpp::log(LoggerCpp::Level::Critical, "Cannot read configuration file: " + configPath);
            LoggerCpp::log(LoggerCpp::Level::Critical, std::string("Error: ") + std::strerror(errno));
            return false;
        }
    }

    const std::vector<std::string> requiredTools = {"mksquashfs", "xorriso", "lslogins"};
    for (const auto &tool : requiredTools) {
        const ProcessRunner::Result r = ProcessRunner::run("which", {tool});
        if (!r.started || r.exitStatus != ProcessRunner::ExitStatus::NormalExit || r.exitCode != 0) {
            LoggerCpp::log(LoggerCpp::Level::Critical, "Required tool not found: " + tool);
            return false;
        }
    }

    const std::vector<std::string> requiredDirs = {"/boot", "/etc", "/usr/lib"};
    for (const auto &d : requiredDirs) {
        if (!DirCpp::exists(d)) {
            LoggerCpp::log(LoggerCpp::Level::Critical, "Required directory not found: " + d);
            return false;
        }
    }

    LoggerCpp::log(LoggerCpp::Level::Debug, "Configuration initialization passed");
    return true;
}

static int validate_configuration_like_settings_qt(SettingsCpp &settings, bool isCliBuild)
{
    // This mirrors Settings::checkConfiguration() (Qt).
    // The "+++ bool Settings::checkConfiguration() const +++" line is emitted by emit_qt_like_settings_runtime_diagnostics.

    SettingsCheckConfigurationCpp::Callbacks ccb;
    ccb.debug = [](const std::string &text) {
        LoggerCpp::log(LoggerCpp::Level::Debug, StringCpp::trimmedLikeQStringUtf8(text));
    };
    ccb.critical = [](const std::string &text) {
        LoggerCpp::log(LoggerCpp::Level::Critical, StringCpp::trimmedLikeQStringUtf8(text));
    };
    if (!SettingsCheckConfigurationCpp::checkConfigurationLikeSettingsQt(settings,
                                                                        isCliBuild,
                                                                        false,
                                                                        ccb)) {
        return fail_configuration_validation_like_settings_qt();
    }

    {
        std::string path = settings.snapshotDir;
        if (StringCpp::endsWithLikeQStringUtf8(path, "/snapshot")) {
            path = StringCpp::removeLikeQStringUtf8(path, static_cast<int>(path.size()) - 9, 9);
        }
        SettingsSpaceCpp::Callbacks cb;
        cb.debug = [](const std::string &text) { (void)StdioCpp::write(stdout, text); };
        cb.critical = [](const std::string &text) { (void)StdioCpp::write(stdout, text); };
        (void)SettingsSpaceCpp::getFreeSpaceStringsLikeSettingsQt(settings, path, cb);
    }

    return EXIT_SUCCESS;
}

static void emit_qt_like_settings_runtime_diagnostics(SettingsCpp &settings, bool emitGetFilename)
{
    (void)settings;
    LoggerCpp::log(LoggerCpp::Level::Debug, "+++ void Settings::loadConfig() +++");
    LoggerCpp::log(LoggerCpp::Level::Debug, "+++ void Settings::setVariables() +++");
    if (emitGetFilename) {
        LoggerCpp::log(LoggerCpp::Level::Debug, "+++ QString Settings::getFilename() const +++");
    }
    LoggerCpp::log(LoggerCpp::Level::Debug, "+++ void Settings::selectKernel() +++");
}

} // namespace

int main(int argc, char **argv)
{
    const std::string applicationName = basename_like_qfileinfo_filename((argc > 0) ? argv[0] : nullptr);
    const std::string organizationName = "MX-Linux";

    if (getuid() == 0) {
        (void)setenv("XDG_RUNTIME_DIR", "/run/user/0", 1);
        (void)unsetenv("SESSION_MANAGER");
        (void)setenv("HOME", "/root", 1);
    }

    std::vector<std::string> argsStd;
    argsStd.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        argsStd.push_back(std::string(argv[i] ? argv[i] : ""));
    }

    const bool wantsHelp = arg_present(argc, argv, "--help") || arg_present(argc, argv, "-h");
    const bool wantsVersion = arg_present(argc, argv, "--version");

    const std::string localeName = locale_name_like_qlocale_name();
    (void)AppTranslatorCpp::loadFromDir("/usr/share/" + applicationName + "/locale", "s4-snapshot", localeName);

    CommandLineParserStd parser;
    parser.setApplicationName(applicationName);

    parser.setApplicationDescription(AppTranslatorCpp::tQt(
        "QObject",
        "Tool used for creating a live-CD from the running system"));

    parser.addHelpOption();
    parser.addVersionOption();

    parser.addOption({{"c", "cli"}, AppTranslatorCpp::tQt("QObject", "Use CLI only")});

    parser.addOption({{"cores"}, AppTranslatorCpp::tQt("QObject", "Number of CPU cores to be used."), "number"});
    parser.addOption({{"d", "directory"}, AppTranslatorCpp::tQt("QObject", "Output directory"), "path"});
    parser.addOption({{"f", "file"}, AppTranslatorCpp::tQt("QObject", "Output filename"), "name"});
    parser.addOption({{"k", "kernel"},
                      (AppTranslatorCpp::tQt("QObject",
                                            "Name a different kernel to use other than the default running kernel, use format returned by '")
                       + std::string("uname -r")
                       + formatPercent1(AppTranslatorCpp::tQt("QObject", "' Or the full path: %1"), "/boot/vmlinuz-x.xx.x...")),
                      AppTranslatorCpp::tQt("QObject", "version, or path")});
    parser.addOption({{"l", "compression-level"},
                      AppTranslatorCpp::tQt("QObject", "Compression level options. Use quotes: \"-Xcompression-level <level>\", or \"-Xalgorithm <algorithm>\", or \"-Xhc\", see mksquashfs man page"),
                      "\"option\""});
    parser.addOption({{"m", "month"},
                      AppTranslatorCpp::tQt("QObject", "Create a monthly snapshot, add 'Month' name in the ISO name, skip used space calculation This option sets reset-accounts and compression to defaults, arguments changing those items will be ignored Optionally specify a suffix to add to the month name (e.g., '1' for 'July.1')")});
    parser.addOption({{"n", "no-checksums"},
                      AppTranslatorCpp::tQt("QObject", "Don't calculate checksums for resulting ISO file")});
    parser.addOption({{"o", "override-size"},
                      AppTranslatorCpp::tQt("QObject", "Skip calculating free space to see if the resulting ISO will fit")});
    parser.addOption({{"p", "preempt"},
                      AppTranslatorCpp::tQt("QObject", "Option to fix issue with calculating checksums on preempt_rt kernels")});
    parser.addOption({{"r", "reset"}, AppTranslatorCpp::tQt("QObject", "Resetting accounts (for distribution to others)")});
    parser.addOption({{"s", "checksums"}, AppTranslatorCpp::tQt("QObject", "Calculate checksums for resulting ISO file")});
    parser.addOption({{"t", "throttle"},
                      AppTranslatorCpp::tQt("QObject", "Throttle the I/O input rate by the given percentage. This can be used to reduce the I/O and CPU consumption of Mksquashfs."),
                      "number"});
    parser.addOption({{"w", "workdir"}, AppTranslatorCpp::tQt("QObject", "Work directory"), "path"});
    parser.addOption({{"datafiles-path"}, "Path to live-files data directory", "path"});
    parser.addOption({{"x", "exclude"},
                      AppTranslatorCpp::tQt("QObject", "Exclude main folders, valid choices: Desktop, Documents, Downloads, Flatpaks, Music, Networks, Pictures, Steam, Videos, VirtualBox. Use the option one time for each item you want to exclude"),
                      AppTranslatorCpp::tQt("QObject", "one item")});
    parser.addOption({{"z", "compression"},
                      AppTranslatorCpp::tQt("QObject", "Compression format, valid choices: lz4, lzo, gzip, xz, zstd"),
                      AppTranslatorCpp::tQt("QObject", "format")});
    parser.addOption({{"shutdown"}, AppTranslatorCpp::tQt("QObject", "Shutdown computer when done.")});

    (void)parser.loadCliParserTranslations("en", "translations/cli_parser/en.kv");

    if (!parser.parse(argsStd)) {
        (void)StdioCpp::write(stdout, parser.errorText() + "\n");
        (void)StdioCpp::write(stderr, parser.helpText() + "\n");
        return EXIT_FAILURE;
    }

    if (wantsHelp) {
        (void)StdioCpp::write(stderr, parser.helpText() + "\n");
        return EXIT_SUCCESS;
    }

    if (wantsVersion) {
        (void)StdioCpp::write(stdout, applicationName + " " + std::string(VERSION) + "\n");
        return EXIT_SUCCESS;
    }

    const std::string compressionValue = parser.value("compression");
    if (!is_allowed_compression(compressionValue)) {
        (void)StdioCpp::write(stdout, "Error: Unsupported compression format: " + compressionValue + "\n");
        (void)StdioCpp::write(stdout, "Supported formats: lz4, lzo, gzip, xz, zstd\n");
        (void)StdioCpp::write(stdout,
                              "Please use one of the supported formats or omit the option to use default (zstd).\n");
        return EXIT_FAILURE;
    }

    LoggerCpp::log(LoggerCpp::Level::Debug,
                   applicationName + " " + AppTranslatorCpp::tQt("QObject", "version:") + " " + std::string(VERSION));
    if (argc > 1) {
        LoggerCpp::log(LoggerCpp::Level::Debug, format_args_like_qt_qdebug_qstringlist(argsStd));
    }

    const ProcessRunner::Result lognameRes = ProcessRunner::run("logname", {}, {}, 30000);
    const std::string logname = StringCpp::trimmedLikeQStringUtf8(lognameRes.stdoutText);
    if (logname == "root") {
        MessageHandlerCpp::showMessage(MessageHandlerCpp::Critical,
                                      AppTranslatorCpp::tQt("main", "Error"),
                                      AppTranslatorCpp::tQt("QObject",
                                                          "You seem to be logged in as root, please log out and log in as normal user to use this program."));
        return EXIT_FAILURE;
    }

    Log log("/tmp/" + applicationName + ".log");

    SettingsCpp settings = SettingsCppBuilder::build(parser, false, applicationName, organizationName);

    if (settings.maxCores == 0) {
        LoggerCpp::log(LoggerCpp::Level::Debug, "+++ bool Settings::initializeConfiguration() +++");
        LoggerCpp::log(LoggerCpp::Level::Critical, "Failed to determine number of CPU cores");
        SettingsInitializationErrorCpp::handleInitializationErrorLikeSettingsQt(applicationName,
                                                                                "Failed to initialize configuration");
        return EXIT_FAILURE;
    }

    if (!initialize_configuration_like_settings_qt()) {
        return fail_initialize_configuration_like_settings_qt("Failed to initialize configuration");
    }

    const std::string currentKernel = get_current_kernel_std();
    const std::vector<std::string> users = SystemInfoCpp::listUsers();

    SettingsProcessArgsCpp::Callbacks paCb;
    paCb.onWarning = [](const std::string &msg) { LoggerCpp::log(LoggerCpp::Level::Warning, msg); };
    paCb.onCriticalMessage = [](const std::string &title, const std::string &msg) {
        MessageHandlerCpp::showMessage(MessageHandlerCpp::Critical, title, msg);
    };

    SettingsProcessArgsCpp::Input paIn = make_process_args_input(parser, currentKernel, users, settings.snapshotName);

    try {
        SettingsProcessArgsCpp::applyLikeSettingsQt(settings, paIn, paCb);
    } catch (...) {
        return EXIT_FAILURE;
    }

    emit_qt_like_settings_runtime_diagnostics(settings, parser.value("file").empty());

    {
        const int v = validate_configuration_like_settings_qt(settings, true);
        if (v != EXIT_SUCCESS) {
            return v;
        }
    }

    const BatchprocessingCpp::ExcludesPromptResult excludesPrompt =
        BatchprocessingCpp::checkUpdatedDefaultExcludesCli(settings, stdin, stdout);
    if (excludesPrompt.shouldExit) {
        return excludesPrompt.exitCode;
    }

    BatchprocessingCppRunner::Callbacks cb;
    cb.debug = [](const std::string &text) { LoggerCpp::log(LoggerCpp::Level::Debug, text); };
    cb.critical = [](const std::string &text) { LoggerCpp::log(LoggerCpp::Level::Critical, text); };

    BatchprocessingCppRunner::Dependencies deps;
    deps.runWork = [](const WorkCppPlan &plan, const WorkCppExecutor::Callbacks &wcb) {
        return WorkCppExecutor::run(plan, wcb);
    };

    const auto r = BatchprocessingCppRunner::runFromSettings(settings, applicationName, cb, deps);

    if (r.aborted) {
        LoggerCpp::log(LoggerCpp::Level::Critical, r.abortReason);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
