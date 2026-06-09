/**********************************************************************
 *  main.cpp
 **********************************************************************
 * Copyright (C) 2015-2025 MX Authors
 *
 * Authors: Adrian
 *          Debian <http://debian.org>
 *
 * This file is part of S4 Snapshot.
 *
 * S4 Snapshot is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * S4 Snapshot is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with S4 Snapshot.  If not, see <http://www.gnu.org/licenses/>.
 **********************************************************************/
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QLocale>
#include <QTranslator>
#include <QString>
#include <QStringList>
#include <QTimer>
#include "command_runner.h"
#include "config_paths.h"
#include "process_runner.h"

#include <csignal>

#ifndef CLI_BUILD
#include "mainwindow.h"
#include "gui_elevation.h"
#include <QApplication>
#include <QIcon>
#include <QMessageBox>
#endif

#include "batchprocessing.h"
#include "batchprocessing_cpp.h"
#include "common.h"
#include "file_cpp.h"
#include "cmd_cpp.h"
#include "log.h"
#ifdef CLI_BUILD
#include "qt_logging_compat_cli.h"
#endif
#ifdef CLI_BUILD
#include "messagehandler_cpp.h"
#else
#include "messagehandler.h"
#endif
#include "settings.h"
#include "settings_cpp_builder.h"
#include "qt_library_info_cpp.h"
#include "work.h"

#include "app_translator_cpp.h"

#ifdef CLI_BUILD
#include "command_line_parser_std.h"
#endif

#ifndef VERSION
    #define VERSION "?.?.?.?"
#endif

static QTranslator qtTran, qtBaseTran, appTran;
QString currentKernel {};

#ifdef CLI_BUILD
static std::string formatPercent1(std::string s, const std::string &arg1)
{
    std::string::size_type pos = 0;
    while ((pos = s.find("%1", pos)) != std::string::npos) {
        s.replace(pos, 2, arg1);
        pos += arg1.size();
    }
    return s;
}
#endif

void checkSquashfs();
void setTranslation();
void signalHandler(int signal);

#ifndef CLI_BUILD
static void setupParser(QCommandLineParser &parser)
{
    parser.setApplicationDescription(QObject::tr("Tool used for creating a live-CD from the running system"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(QCommandLineOption(QStringList {QStringLiteral("c"), QStringLiteral("cli")},
                                        QObject::tr("Use CLI only")));

    parser.addOption(QCommandLineOption(QStringList {QStringLiteral("cores")},
                                        QObject::tr("Number of CPU cores to be used."),
                                        QStringLiteral("number")));
    parser.addOption(QCommandLineOption(QStringList {QStringLiteral("d"), QStringLiteral("directory")},
                                        QObject::tr("Output directory"),
                                        QStringLiteral("path")));
    parser.addOption(QCommandLineOption(QStringList {QStringLiteral("f"), QStringLiteral("file")},
                                        QObject::tr("Output filename"),
                                        QStringLiteral("name")));
    parser.addOption(QCommandLineOption(QStringList {QStringLiteral("k"), QStringLiteral("kernel")},
                                        QObject::tr("Name a different kernel to use other than the default running kernel, use format returned by ")
                                                + "'uname -r'" + " " + QObject::tr("Or the full path: %1").arg("/boot/vmlinuz-x.xx.x..."),
                                        QStringLiteral("version, or path")));
    parser.addOption(QCommandLineOption(QStringList {QStringLiteral("l"), QStringLiteral("compression-level")},
                                        QObject::tr("Compression level options. Use quotes: \"-Xcompression-level <level>\", or \"-Xalgorithm <algorithm>\", or \"-Xhc\", see mksquashfs man page"),
                                        QStringLiteral("\"option\"")));
    parser.addOption(QCommandLineOption(QStringList {QStringLiteral("m"), QStringLiteral("month")},
                                        QObject::tr("Create a monthly snapshot, add 'Month' name in the ISO name, skip used space calculation This option sets reset-accounts and compression to defaults, arguments changing those items will be ignored Optionally specify a suffix to add to the month name (e.g., '1' for 'July.1')")));
    parser.addOption(QCommandLineOption(QStringList {QStringLiteral("n"), QStringLiteral("no-checksums")},
                                        QObject::tr("Don't calculate checksums for resulting ISO file")));
    parser.addOption(QCommandLineOption(QStringList {QStringLiteral("o"), QStringLiteral("override-size")},
                                        QObject::tr("Skip calculating free space to see if the resulting ISO will fit")));
    parser.addOption(QCommandLineOption(QStringList {QStringLiteral("p"), QStringLiteral("preempt")},
                                        QObject::tr("Option to fix issue with calculating checksums on preempt_rt kernels")));
    parser.addOption(QCommandLineOption(QStringList {QStringLiteral("r"), QStringLiteral("reset")},
                                        QObject::tr("Resetting accounts (for distribution to others)")));
    parser.addOption(QCommandLineOption(QStringList {QStringLiteral("s"), QStringLiteral("checksums")},
                                        QObject::tr("Calculate checksums for resulting ISO file")));
    parser.addOption(QCommandLineOption(QStringList {QStringLiteral("t"), QStringLiteral("throttle")},
                                        QObject::tr("Throttle the I/O input rate by the given percentage. This can be used to reduce the I/O and CPU consumption of Mksquashfs."),
                                        QStringLiteral("number")));
    parser.addOption(QCommandLineOption(QStringList {QStringLiteral("w"), QStringLiteral("workdir")},
                                        QObject::tr("Work directory"),
                                        QStringLiteral("path")));
    parser.addOption(QCommandLineOption(QStringList {QStringLiteral("x"), QStringLiteral("exclude")},
                                        QObject::tr("Exclude main folders, valid choices: Desktop, Documents, Downloads, Flatpaks, Music, Networks, Pictures, Steam, Videos, VirtualBox. Use the option one time for each item you want to exclude"),
                                        QObject::tr("one item")));
    parser.addOption(QCommandLineOption(QStringList {QStringLiteral("z"), QStringLiteral("compression")},
                                        QObject::tr("Compression format, valid choices: lz4, lzo, gzip, xz, zstd"),
                                        QObject::tr("format")));
    parser.addOption(QCommandLineOption(QStringList {QStringLiteral("shutdown")},
                                        QObject::tr("Shutdown computer when done.")));
}
#endif

int main(int argc, char *argv[])
{
    if (getuid() == 0) {
        qputenv("XDG_RUNTIME_DIR", "/run/user/0");
        qunsetenv("SESSION_MANAGER");
        qputenv("HOME", "/root");
    }

    // Parse arguments early so --help/--version can exit without creating a GUI application.
    // Note: QCommandLineParser help text is translated when options/descriptions are created,
    // so for localized help/version we construct a temporary QCoreApplication and build the parser after installing translators.
    QStringList arguments;
    arguments.reserve(argc);
    for (int i = 0; i < argc; ++i) {
        arguments << QString::fromLocal8Bit(argv[i]);
    }
    const bool wantsHelp = arguments.contains(QLatin1String("--help")) || arguments.contains(QLatin1String("-h"));
    const bool wantsVersion = arguments.contains(QLatin1String("--version"));

    const std::array<int, 3> signalList {SIGINT, SIGTERM, SIGHUP}; // allow SIGQUIT CTRL-\?
    for (auto signalName : signalList) {
        signal(signalName, signalHandler);
    }

    const ProcessRunner::Result lognameRes = ProcessRunner::run("logname", {}, {}, 30000);
    const QString logname = QString::fromStdString(lognameRes.stdoutText).trimmed();

    QCoreApplication::setApplicationVersion(VERSION);
    QCoreApplication::setApplicationName(QString::fromStdString(FileCpp::baseName(std::string(argv[0] ? argv[0] : ""))));
    QCoreApplication::setOrganizationName(S4SnapshotConfig::kOrganizationName);

#ifdef CLI_BUILD
    CommandLineParserStd parser;
    parser.setApplicationName(FileCpp::baseName(std::string(argv[0] ? argv[0] : "")));

    const QString localeNameQt = QLocale().name();
    const std::string localeNameStd = localeNameQt.toStdString();
    const std::string appNameStd = QCoreApplication::applicationName().toStdString();
    (void)AppTranslatorCpp::loadFromDir("/usr/share/" + appNameStd + "/locale", "s4-snapshot", localeNameStd);

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
    parser.addOption({{"n", "no-checksums"}, AppTranslatorCpp::tQt("QObject", "Don't calculate checksums for resulting ISO file")});
    parser.addOption({{"o", "override-size"}, AppTranslatorCpp::tQt("QObject", "Skip calculating free space to see if the resulting ISO will fit")});
    parser.addOption({{"p", "preempt"}, AppTranslatorCpp::tQt("QObject", "Option to fix issue with calculating checksums on preempt_rt kernels")});
    parser.addOption({{"r", "reset"}, AppTranslatorCpp::tQt("QObject", "Resetting accounts (for distribution to others)")});
    parser.addOption({{"s", "checksums"}, AppTranslatorCpp::tQt("QObject", "Calculate checksums for resulting ISO file")});
    parser.addOption({{"t", "throttle"},
                      AppTranslatorCpp::tQt("QObject", "Throttle the I/O input rate by the given percentage. This can be used to reduce the I/O and CPU consumption of Mksquashfs."),
                      "number"});
    parser.addOption({{"w", "workdir"}, AppTranslatorCpp::tQt("QObject", "Work directory"), "path"});
    parser.addOption({{"x", "exclude"},
                      AppTranslatorCpp::tQt("QObject", "Exclude main folders, valid choices: Desktop, Documents, Downloads, Flatpaks, Music, Networks, Pictures, Steam, Videos, VirtualBox. Use the option one time for each item you want to exclude"),
                      AppTranslatorCpp::tQt("QObject", "one item")});
    parser.addOption({{"z", "compression"},
                      AppTranslatorCpp::tQt("QObject", "Compression format, valid choices: lz4, lzo, gzip, xz, zstd"),
                      AppTranslatorCpp::tQt("QObject", "format")});
    parser.addOption({{"shutdown"}, AppTranslatorCpp::tQt("QObject", "Shutdown computer when done.")});

    // Load vendored cli parser translations (best effort). Locale selection will be refined later.
    (void)parser.loadCliParserTranslations("en", "translations/cli_parser/en.kv");

    if (wantsHelp || wantsVersion) {
        std::vector<std::string> argsStd;
        argsStd.reserve(static_cast<size_t>(arguments.size()));
        for (const auto &a : arguments) {
            argsStd.push_back(a.toStdString());
        }
        if (!parser.parse(argsStd)) {
            qWarning().noquote() << QString::fromStdString(parser.errorText());
            qInfo().noquote() << QString::fromStdString(parser.helpText());
            return EXIT_FAILURE;
        }

        if (wantsHelp) {
            qInfo().noquote() << QString::fromStdString(parser.helpText());
            return EXIT_SUCCESS;
        }
        if (wantsVersion) {
            printf("%s %s\n", qPrintable(QCoreApplication::applicationName()),
                   qPrintable(QCoreApplication::applicationVersion()));
            return EXIT_SUCCESS;
        }
    }

    std::vector<std::string> argsStd;
    argsStd.reserve(static_cast<size_t>(arguments.size()));
    for (const auto &a : arguments) {
        argsStd.push_back(a.toStdString());
    }
    if (!parser.parse(argsStd)) {
        qWarning().noquote() << QString::fromStdString(parser.errorText());
        qInfo().noquote() << QString::fromStdString(parser.helpText());
        return EXIT_FAILURE;
    }

    const QString compressionValue = QString::fromStdString(parser.value("compression"));
    const QStringList allowedComp {"lz4", "lzo", "gzip", "xz", "zstd"};
    if (!compressionValue.isEmpty() && !allowedComp.contains(compressionValue)) {
        qDebug() << "Error: Unsupported compression format:" << compressionValue;
        qDebug() << "Supported formats:" << allowedComp.join(", ");
        qDebug() << "Please use one of the supported formats or omit the option to use default (zstd).";
        return EXIT_FAILURE;
    }

    QCoreApplication *app = new QCoreApplication(argc, argv);

    if (logname == "root") {
        const QString message = QString::fromStdString(AppTranslatorCpp::tQt(
            "QObject",
            "You seem to be logged in as root, please log out and log in as normal user to use this program."));
#ifdef CLI_BUILD
        MessageHandlerCpp::showMessage(MessageHandlerCpp::Critical,
                                       AppTranslatorCpp::tQt("QObject", "Error"),
                                       message.toStdString());
#else
        MessageHandler::showMessage(MessageHandler::Critical, QObject::tr("Error"), message);
#endif
        return EXIT_FAILURE;
    }

    setTranslation();
    checkSquashfs();

#ifdef CLI_BUILD
    const Log setLog(("/tmp/" + app->applicationName() + ".log").toStdString());
#else
    const Log setLog("/tmp/" + app->applicationName() + ".log");
    qInstallMessageHandler(Log::messageHandler);
#endif
    qDebug().noquote() << app->applicationName()
                       << QString::fromStdString(AppTranslatorCpp::tQt("QObject", "version:"))
                       << app->applicationVersion();
    if (argc > 1) {
        qDebug().noquote() << "Args:" << app->arguments();
    }

    const SettingsCpp settingsCpp = SettingsCppBuilder::build(
        parser,
        false,
        QCoreApplication::applicationName().toStdString(),
        QCoreApplication::organizationName().toStdString());
    const BatchprocessingCpp::ExcludesPromptResult excludesPrompt =
        BatchprocessingCpp::checkUpdatedDefaultExcludesCli(settingsCpp, stdin, stdout);
    if (excludesPrompt.shouldExit) {
        QCoreApplication::exit(excludesPrompt.exitCode);
    }

    Settings settings(parser, false, QCoreApplication::applicationName(), QCoreApplication::organizationName());
    Batchprocessing batch(&settings);
    QTimer::singleShot(0, app, &QCoreApplication::quit);
    return app->exec();
#else
    if (wantsHelp || wantsVersion) {
        QCoreApplication tempApp(argc, argv);
        setTranslation();

        QCommandLineParser parser;
        setupParser(parser);
        parser.process(tempApp);

        if (wantsHelp) {
            fputs(qPrintable(parser.helpText()), stdout);
            return EXIT_SUCCESS;
        }
        if (wantsVersion) {
            printf("%s %s\n", qPrintable(QCoreApplication::applicationName()),
                   qPrintable(QCoreApplication::applicationVersion()));
            return EXIT_SUCCESS;
        }
    }

    QCommandLineParser parser;
    setupParser(parser);

    if (!parser.parse(arguments)) {
        fprintf(stderr, "%s\n", qPrintable(parser.errorText()));
        return EXIT_FAILURE;
    }
    const QString compressionValue = parser.value("compression");
    const QStringList allowedComp {"lz4", "lzo", "gzip", "xz", "zstd"};
    if (!compressionValue.isEmpty() && !allowedComp.contains(compressionValue)) {
        qDebug() << "Error: Unsupported compression format:" << compressionValue;
        qDebug() << "Supported formats:" << allowedComp.join(", ");
        qDebug() << "Please use one of the supported formats or omit the option to use default (zstd).";
        return EXIT_FAILURE;
    }

    QCoreApplication *app;
#ifdef CLI_BUILD
    app = new QCoreApplication(argc, argv);
#else
    // Determine if we should run in CLI mode based on multiple factors
    const bool forceCliMode = parser.isSet("cli") ||
                              QString(argv[0]).contains("cli") ||
                              !qEnvironmentVariableIsEmpty("MX_SNAPSHOT_CLI");
    const bool noWindowSystem = qEnvironmentVariableIsEmpty("DISPLAY") &&
                                qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY");
    const QString qpa = QString::fromLocal8Bit(qgetenv("QT_QPA_PLATFORM"));
    const bool headlessQpa = (qpa == QLatin1String("offscreen") ||
                              qpa == QLatin1String("minimal") ||
                              qpa == QLatin1String("linuxfb"));
    const bool useCliMode = forceCliMode || noWindowSystem || headlessQpa;

    if (useCliMode) {
        app = new QCoreApplication(argc, argv);
    } else {
        // Set Qt platform to XCB (X11) if not already set and we're in X11 environment
        if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
            if (!qEnvironmentVariableIsEmpty("DISPLAY") && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
                qputenv("QT_QPA_PLATFORM", "xcb");
            }
        }
        app = new QApplication(argc, argv);
        QApplication::setWindowIcon(QIcon::fromTheme("s4-snapshot"));
        QApplication::setApplicationDisplayName(QObject::tr("S4 Snapshot"));
    }
#endif

    if (logname == "root") {
        const QString message = QObject::tr(
            "You seem to be logged in as root, please log out and log in as normal user to use this program.");
#ifdef CLI_BUILD
        MessageHandlerCpp::showMessage(MessageHandlerCpp::Critical, QObject::tr("Error").toStdString(), message.toStdString());
#else
        MessageHandler::showMessage(MessageHandler::Critical, QObject::tr("Error"), message);
#endif
        return EXIT_FAILURE;
    }

    setTranslation();
    checkSquashfs();

    const bool isGuiApp = QCoreApplication::instance()->inherits("QApplication");
    const bool hasAuthTools = !CommandRunner::elevationTool().empty();
    if (getuid() != 0 && (!isGuiApp || !hasAuthTools)) {
        qDebug().noquote() << QString::fromStdString(AppTranslatorCpp::tQt(
                                 "QObject",
                                 "No supported elevation tool found (sudo/doas/gksu)."));
        return EXIT_FAILURE;
    }

#ifdef CLI_BUILD
    const Log setLog(("/tmp/" + app->applicationName() + ".log").toStdString());
#else
    const Log setLog("/tmp/" + app->applicationName() + ".log");
    qInstallMessageHandler(Log::messageHandler);
#endif
#ifdef CLI_BUILD
    qDebug().noquote() << app->applicationName()
                       << QString::fromStdString(AppTranslatorCpp::tQt("QObject", "version:"))
                       << app->applicationVersion();
#else
    qDebug().noquote() << app->applicationName() << QObject::tr("version:") << app->applicationVersion();
#endif
    if (argc > 1) {
        qDebug().noquote() << "Args:" << app->arguments();
    }

    if (isGuiApp) {
        GuiElevation::install();
    }

    // Create settings instance for dependency injection
    Settings settings(parser, isGuiApp, QCoreApplication::applicationName(), QCoreApplication::organizationName());

    if (!isGuiApp) {
        Batchprocessing batch(&settings);
        QTimer::singleShot(0, app, &QCoreApplication::quit);
        return app->exec();
    }
#ifndef CLI_BUILD
    else {
        MainWindow w(&settings);
        w.show();
        return app->exec();
    }
#endif
    return EXIT_SUCCESS;
#endif
}

void setTranslation()
{
    const QString localeName = QLocale().name();
    const QString translationsPath = QString::fromStdString(QtLibraryInfoCpp::translationsPath());
    const QString appName = QCoreApplication::applicationName();

    if (qtTran.load("qt_" + localeName, translationsPath)) {
        QCoreApplication::installTranslator(&qtTran);
    }

    if (qtBaseTran.load("qtbase_" + localeName, translationsPath)) {
        QCoreApplication::installTranslator(&qtBaseTran);
    }

    if (appTran.load("s4-snapshot_" + localeName, "/usr/share/" + appName + "/locale")) {
        QCoreApplication::installTranslator(&appTran);
    }
}

void checkSquashfs()
{
    {
        const ProcessRunner::Result r = ProcessRunner::run("uname", {"-r"}, std::string(), 30000);
        currentKernel = QString::fromStdString(r.stdoutText).trimmed();
    }

    const QString configPath = "/boot/config-" + currentKernel;
    if (FileCpp::exists(configPath.toStdString())
        && ProcessRunner::execute("grep", {"-q", "^CONFIG_SQUASHFS=[ym]", configPath.toStdString()}) != 0) {
        const QString message = QString::fromStdString(AppTranslatorCpp::tQt(
            "QObject",
            "Current kernel doesn't support Squashfs, cannot continue."));
#ifdef CLI_BUILD
        MessageHandlerCpp::showMessage(MessageHandlerCpp::Critical,
                                       AppTranslatorCpp::tQt("QObject", "Error"),
                                       message.toStdString());
#else
        MessageHandler::showMessage(MessageHandler::Critical, QObject::tr("Error"), message);
#endif
        return;
    }
}

void signalHandler(int signal)
{
    const auto signame = strsignal(signal);
    qDebug() << "\nReceived signal:" << (signame ? signame : "Unknown signal");
    QCoreApplication::quit();
}
