/**********************************************************************
 *  settings.cpp
 **********************************************************************
 * Copyright (C) 2020-2025 MX Authors
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

#include "settings.h"

#include <QCommandLineParser>
#include <QDate>
#ifndef CLI_BUILD
#include <QDebug>
#else
#include "qt_logging_compat_cli.h"
#endif
#ifndef CLI_BUILD
#include <QGuiApplication>
#endif
#include <QJsonDocument>
#include <QJsonObject>

#include <exception>
#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>
#include <iostream>

#include "filesystemutils_cpp.h"
#include "dir_cpp.h"
#include "file_cpp.h"
#include "standard_paths_cpp.h"
#include "datetime_cpp.h"
#include "qsettings_cpp.h"
#include "app_translator_cpp.h"
#ifdef CLI_BUILD
#include "command_line_parser_std.h"
#include "messagehandler_cpp.h"
#else
#include "messagehandler.h"
#endif
#include "command_runner.h"
#include "process_runner.h"
#include "work_cpp_utils.h"
#include "string_cpp.h"
#include "systeminfo_cpp.h"

namespace
{
[[maybe_unused]] std::vector<std::string> toStdVector(const QStringList &list)
{
    std::vector<std::string> out;
    out.reserve(static_cast<size_t>(list.size()));
    for (const QString &s : list) {
        out.push_back(s.toStdString());
    }
    return out;
}

QString etcBaseDir()
{
#ifdef UNIT_TESTS
    const QString ut = Settings::ut_etcDirOverride();
    if (!ut.isEmpty()) {
        return ut;
    }
#endif
    return QStringLiteral("/etc");
}

bool isSymlinkPosix(const QString &path)
{
    if (path.isEmpty()) {
        return false;
    }
    struct stat st;
    return ::lstat(path.toLocal8Bit().constData(), &st) == 0 && S_ISLNK(st.st_mode);
}

QString linuxfsCompressionNameFromTypeCode(const QString &code)
{
    if (code == QLatin1String("1")) {
        return QStringLiteral("gzip");
    }
    if (code == QLatin1String("2")) {
        return QStringLiteral("lzo");
    }
    if (code == QLatin1String("3")) {
        return QStringLiteral("lzma");
    }
    if (code == QLatin1String("4")) {
        return QStringLiteral("xz");
    }
    if (code == QLatin1String("5")) {
        return QStringLiteral("lz4");
    }
    if (code == QLatin1String("6")) {
        return QStringLiteral("zstd");
    }
    return {};
}

QString loggedInUserName()
{
    return QString::fromStdString(CommandRunner::loggedInUserName());
}

QString userConfigBaseDir()
{
#ifdef UNIT_TESTS
    const QString ut = Settings::ut_userConfigBaseDirOverride();
    if (!ut.isEmpty()) {
        return ut;
    }
#endif
    const QString username = loggedInUserName();

    if (!username.isEmpty()) {
        const QString candidateHome = QString::fromLocal8Bit(DirCpp::cleanPath(("/home/" + username).toStdString()).c_str());
        if (DirCpp::exists(candidateHome.toStdString())) {
            return QString::fromLocal8Bit(DirCpp::cleanPath((candidateHome + "/.config").toStdString()).c_str());
        }
    }

    return QString::fromLocal8Bit(StandardPathsCpp::writableConfigLocation().c_str());
}

void chownFileToLoggedInUser(const QString &path)
{
    const QString username = loggedInUserName();
    if (username.isEmpty() || path.isEmpty()) {
        return;
    }

    // Qt QFileInfo::exists/isWritable replacement for backend logic.
    if (!FileCpp::exists(path.toStdString()) || ::access(path.toLocal8Bit().constData(), W_OK) == 0) {
        return;
    }
    (void)CommandRunner::procAsRoot("chown",
                                   std::vector<std::string>{(username + ":").toStdString(), path.toStdString()},
                                   std::string(),
                                   CommandRunner::QuietMode::Yes);
}

std::vector<std::string> buildExecutableSearchPath()
{
    const char *env = std::getenv("PATH");
    const std::string pathEnv = (env != nullptr ? std::string(env) : std::string());
    std::vector<std::string> out = StringCpp::splitLikeQString(pathEnv, ":", StringCpp::SplitBehavior::KeepEmptyParts);
    out.push_back("/usr/sbin");
    return out;
}

QString trimmedLikeQt(const QString &s)
{
    const QByteArray utf8 = s.toUtf8();
    const std::string trimmed = StringCpp::trimmedLikeQStringUtf8(std::string(utf8.constData(), static_cast<size_t>(utf8.size())));
    return QString::fromUtf8(trimmed.c_str(), static_cast<qsizetype>(trimmed.size()));
}

bool startsWithLikeQt(const QString &s, const QString &prefix)
{
    const QByteArray sUtf8 = s.toUtf8();
    const QByteArray pUtf8 = prefix.toUtf8();
    return StringCpp::startsWithLikeQStringUtf8(
        std::string(sUtf8.constData(), static_cast<size_t>(sUtf8.size())),
        std::string(pUtf8.constData(), static_cast<size_t>(pUtf8.size())));
}

bool endsWithLikeQt(const QString &s, const QString &suffix)
{
    const QByteArray sUtf8 = s.toUtf8();
    const QByteArray pUtf8 = suffix.toUtf8();
    return StringCpp::endsWithLikeQStringUtf8(
        std::string(sUtf8.constData(), static_cast<size_t>(sUtf8.size())),
        std::string(pUtf8.constData(), static_cast<size_t>(pUtf8.size())));
}

bool startsWithLikeQt(const QString &s, QChar prefix)
{
    const QByteArray sUtf8 = s.toUtf8();
    const QByteArray pUtf8 = QString(prefix).toUtf8();
    return StringCpp::startsWithLikeQStringUtf8(
        std::string(sUtf8.constData(), static_cast<size_t>(sUtf8.size())),
        std::string(pUtf8.constData(), static_cast<size_t>(pUtf8.size())));
}

bool endsWithLikeQt(const QString &s, QChar suffix)
{
    const QByteArray sUtf8 = s.toUtf8();
    const QByteArray pUtf8 = QString(suffix).toUtf8();
    return StringCpp::endsWithLikeQStringUtf8(
        std::string(sUtf8.constData(), static_cast<size_t>(sUtf8.size())),
        std::string(pUtf8.constData(), static_cast<size_t>(pUtf8.size())));
}

QString removeLikeQt(const QString &s, qsizetype pos, qsizetype len)
{
    const QByteArray sUtf8 = s.toUtf8();
    const std::string out = StringCpp::removeLikeQStringUtf8(
        std::string(sUtf8.constData(), static_cast<size_t>(sUtf8.size())),
        static_cast<int>(pos),
        static_cast<int>(len));
    return QString::fromUtf8(out.c_str(), static_cast<qsizetype>(out.size()));
}

QString removeAllLikeQt(const QString &s, const QString &needle)
{
    const QByteArray sUtf8 = s.toUtf8();
    const QByteArray nUtf8 = needle.toUtf8();
    const std::string out = StringCpp::removeAllLikeQStringUtf8(
        std::string(sUtf8.constData(), static_cast<size_t>(sUtf8.size())),
        std::string(nUtf8.constData(), static_cast<size_t>(nUtf8.size())));
    return QString::fromUtf8(out.c_str(), static_cast<qsizetype>(out.size()));
}

} // namespace

quint8 Settings::compressionFactorValue(const QString &compressionName) const
{
    // Must match QHash<QString, quint8>::value(key) semantics: returns 0 if not found.
    for (const auto &kv : compressionFactorTable) {
        if (compressionName == QLatin1String(kv.first)) {
            return static_cast<quint8>(kv.second);
        }
    }
    return 0;
}

#ifdef UNIT_TESTS
namespace {
static QString g_ut_user_config_base_dir_override;
static QString g_ut_excludes_source_path_override;
static QString g_ut_fallback_excludes_path_override;
static QString g_ut_etc_dir_override;
static QString g_ut_gui_editor_override;
static QString g_ut_snapshot_basename_override;
static QString g_ut_stamp_override;
static QStringList g_ut_users_override;
}

Settings::Settings()
    : applicationName(QStringLiteral("unit_tests")),
      organizationName(QStringLiteral("MX-Linux")),
      x86(false),
      maxCores(1),
      monthly(false),
      overrideSize(false),
      editBootMenu(false),
      isGuiApp(false),
      forceInstaller(false),
      live(SystemInfoCpp::isLive()),
      makeIsohybrid(false),
      configFilePath(),
      guiEditor(),
      snapshotBasename(),
      stamp(),
      path(buildExecutableSearchPath()),
      users()
{
}

void Settings::ut_setUserConfigBaseDirOverride(const QString &dir)
{
    g_ut_user_config_base_dir_override = dir;
}

QString Settings::ut_userConfigBaseDirOverride()
{
    return g_ut_user_config_base_dir_override;
}

void Settings::ut_setConfigFilePath(const QString &path)
{
    configFilePath = path;
}

void Settings::ut_setExcludesSourcePathOverride(const QString &path)
{
    g_ut_excludes_source_path_override = path;
}

QString Settings::ut_excludesSourcePathOverride()
{
    return g_ut_excludes_source_path_override;
}

void Settings::ut_setFallbackExcludesPathOverride(const QString &path)
{
    g_ut_fallback_excludes_path_override = path;
}

QString Settings::ut_fallbackExcludesPathOverride()
{
    return g_ut_fallback_excludes_path_override;
}

void Settings::ut_setEtcDirOverride(const QString &dir)
{
    g_ut_etc_dir_override = dir;
}

QString Settings::ut_etcDirOverride()
{
    return g_ut_etc_dir_override;
}

void Settings::ut_setGuiEditorOverride(const QString &editor)
{
    g_ut_gui_editor_override = editor;
}

QString Settings::ut_guiEditorOverride()
{
    return g_ut_gui_editor_override;
}

void Settings::ut_setSnapshotBasenameOverride(const QString &basename)
{
    g_ut_snapshot_basename_override = basename;
}

QString Settings::ut_snapshotBasenameOverride()
{
    return g_ut_snapshot_basename_override;
}

void Settings::ut_setStampOverride(const QString &stamp)
{
    g_ut_stamp_override = stamp;
}

QString Settings::ut_stampOverride()
{
    return g_ut_stamp_override;
}

void Settings::ut_setUsersOverride(const QStringList &users)
{
    g_ut_users_override = users;
}

QStringList Settings::ut_usersOverride()
{
    return g_ut_users_override;
}
#endif

Settings::Settings(
#ifdef CLI_BUILD
    const CommandLineParserStd &argParser,
#else
    const QCommandLineParser &argParser,
#endif
    bool isGuiApp,
    const QString &applicationName,
    const QString &organizationName)
    : applicationName(applicationName),
      organizationName(organizationName),
      x86(SystemInfoCpp::is386()),
      maxCores(trimmedLikeQt(QString::fromStdString(CommandRunner::getOut("nproc", CommandRunner::QuietMode::Yes))).toUInt()),
      monthly(argParser.isSet("month")),
      overrideSize(argParser.isSet("override-size")),
      editBootMenu(getEditBootMenuSetting()),
      isGuiApp(isGuiApp),
      forceInstaller(getInitialSettings().forceInstaller),
      live(getInitialSettings().live),
      makeIsohybrid(getInitialSettings().makeIsohybrid),
      configFilePath("/etc/" + this->applicationName + ".conf"),
      guiEditor(getInitialSettings().guiEditor),
      snapshotBasename(getInitialSettings().snapshotBasename),
      stamp(getInitialSettings().stamp),
      path(buildExecutableSearchPath()),
      users(getInitialSettings().users)
{
    try {
        if (!initializeConfiguration()) {
#ifdef CLI_BUILD
            handleInitializationError(QString::fromStdString(AppTranslatorCpp::tQt(
                "QObject",
                "Failed to initialize configuration")));
#else
            handleInitializationError(QObject::tr("Failed to initialize configuration"));
#endif
            exit(EXIT_FAILURE);
        }

        const QString appName = this->applicationName;
        const QString overlayBase = "/run/" + appName + "/bind-root-overlay";
        bool cleanupRan = false;
        bool cleanupOk = true;
        const QString elevateTool = QString::fromStdString(CommandRunner::elevationTool());
        if (FileCpp::exists(std::string("/tmp/installed-to-live/cleanup.conf")) || FileCpp::exists(overlayBase.toStdString())) {
            cleanupRan = true;
            cleanupOk = CommandRunner::run((elevateTool + " /usr/lib/" + appName + "/snapshot-lib cleanup").toStdString());
        }
        const QString overlayRoot = "/run/" + appName + "/bind-root-overlay/root";
        const bool bindRootMounted = CommandRunner::run("mountpoint -q /.bind-root", CommandRunner::QuietMode::Yes)
            || CommandRunner::run(("mountpoint -q \"" + overlayRoot + "\"").toStdString(), CommandRunner::QuietMode::Yes);
        if (!cleanupRan || cleanupOk || !bindRootMounted) {
            (void)CommandRunner::run((elevateTool + " /usr/lib/" + appName + "/snapshot-lib cleanup_overlay " + appName).toStdString());
        }

        loadConfig(); // Load settings from .conf file
        setVariables();
        kernel = getInitialKernel(argParser); // Initialize kernel after config is loaded
        preempt = argParser.isSet("preempt"); // Initialize preempt from command line
        processArgs(argParser);

        if (monthly) {
            setMonthlySnapshot(argParser);
        }

        processExclArgs(argParser);

        // Validate final configuration
        if (!checkConfiguration()) {
#ifdef CLI_BUILD
            handleInitializationError(QString::fromStdString(AppTranslatorCpp::tQt(
                "QObject",
                "Configuration validation failed")));
#else
            handleInitializationError(QObject::tr("Configuration validation failed"));
#endif
            exit(EXIT_FAILURE);
        }

    } catch (const std::exception &e) {
#ifdef CLI_BUILD
        handleInitializationError(QString::fromStdString(AppTranslatorCpp::tQt(
                                     "QObject",
                                     "Exception during initialization: %1"))
                                     .arg(e.what()));
#else
        handleInitializationError(QObject::tr("Exception during initialization: %1").arg(e.what()));
#endif
        exit(EXIT_FAILURE);
    } catch (...) {
#ifdef CLI_BUILD
        handleInitializationError(QString::fromStdString(AppTranslatorCpp::tQt(
            "QObject",
            "Unknown exception during initialization")));
#else
        handleInitializationError(QObject::tr("Unknown exception during initialization"));
#endif
        exit(EXIT_FAILURE);
    }
}

// Check if compression is available in the kernel (lz4, lzo, xz)
bool Settings::checkCompression() const
{
    if (compression == "gzip"
        || !FileCpp::exists(("/boot/config-" + kernel).toStdString())) { // Don't check for gzip or if the kernel config file is missing
        return true;
    }
    return CommandRunner::run(("grep ^CONFIG_SQUASHFS_" + compression.toUpper() + "=y /boot/config-" + kernel).toStdString());
}

// Adds or removes exclusion to the exclusion string
void Settings::addRemoveExclusion(bool add, QString exclusion)
{
    if (startsWithLikeQt(exclusion, QLatin1Char('/'))) {
        exclusion = removeLikeQt(exclusion, 0, 1);
    }
    if (add) {
        sessionExcludes.append('"' + exclusion + "\" ");
    } else {
        sessionExcludes = removeAllLikeQt(sessionExcludes, '"' + exclusion + "\" ");
    }
}

bool Settings::checkSnapshotDir() const
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    const CommandRunner::Result mk = CommandRunner::procAsRoot(
        "mkdir",
        std::vector<std::string>{"-p", snapshotDir.toStdString()},
        std::string(),
        CommandRunner::QuietMode::No);
    if (!(mk.started && mk.normalExit && mk.exitCode == 0)) {
#ifdef CLI_BUILD
        qDebug() << (QString::fromStdString(AppTranslatorCpp::tQt("QObject", "Could not create work directory. ")) + snapshotDir);
#else
        qDebug() << QObject::tr("Could not create work directory. ") + snapshotDir;
#endif
        return false;
    }
    const QString username = loggedInUserName();
    if (!username.isEmpty()) {
        (void)CommandRunner::procAsRoot(
            "chown",
            std::vector<std::string>{(username + ":").toStdString(), snapshotDir.toStdString()},
            std::string(),
            CommandRunner::QuietMode::Yes);
    }
    return true;
}

bool Settings::checkTempDir()
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    // Set workdir location if not defined in .conf file, doesn't exist, or not supported partition
    if (tempDirParent.isEmpty() || !FileCpp::exists(tempDirParent.toStdString())
        || !FileSystemUtilsCpp::isOnSupportedPartition(tempDirParent.toStdString())) {
        tempDirParent = FileSystemUtilsCpp::isOnSupportedPartition(snapshotDir.toStdString())
            ? QString::fromStdString(FileSystemUtilsCpp::largerFreeSpace("/tmp", "/home", snapshotDir.toStdString()))
            : QString::fromStdString(FileSystemUtilsCpp::largerFreeSpace("/tmp", "/home"));
    }
    if (tempDirParent == "/home") {
        QString userName = trimmedLikeQt(QString::fromUtf8(qgetenv("SUDO_USER")));
        if (userName.isEmpty()) {
            userName = trimmedLikeQt(QString::fromUtf8(qgetenv("LOGNAME")));
        }
        tempDirParent = "/home/" + userName;
    }
    tmpdir.reset(new TempDir((tempDirParent + "/s4-snapshot-XXXXXXXX").toLocal8Bit().toStdString()));
    if (!tmpdir->isValid()) {
#ifdef CLI_BUILD
        qCritical() << QString::fromStdString(AppTranslatorCpp::tQt("QObject", "Could not create temp directory:"))
                    << QString::fromLocal8Bit(tmpdir->path().c_str());
        qCritical() << QString::fromStdString(AppTranslatorCpp::tQt(
            "QObject",
            "Please check that the parent directory exists and is writable:"));
#else
        qCritical() << QObject::tr("Could not create temp directory:") << QString::fromLocal8Bit(tmpdir->path().c_str());
        qCritical() << QObject::tr("Please check that the parent directory exists and is writable:");
#endif
        qCritical() << tempDirParent;
        return false;
    }
    workDir = QString::fromLocal8Bit(tmpdir->path().c_str());
    freeSpaceWork = static_cast<quint64>(FileSystemUtilsCpp::getFreeSpaceKiB(workDir.toStdString()));

    (void)DirCpp().mkpath((workDir + "/iso-template/antiX").toStdString());
    qDebug() << "Work directory is placed in" << tempDirParent;
    return true;
}

bool Settings::checkConfiguration() const
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";

    // Check compression format
    if (!checkCompression()) {
#ifdef CLI_BUILD
        qCritical() << QString::fromStdString(AppTranslatorCpp::tQt(
                           "QObject",
                           "Compression format '%1' is not supported by the current kernel"))
                           .arg(compression);
#else
        qCritical() << QObject::tr("Compression format '%1' is not supported by the current kernel").arg(compression);
#endif
        return false;
    }

    // Check cores setting
    if (cores == 0 || cores > maxCores) {
#ifdef CLI_BUILD
        qCritical() << QString::fromStdString(AppTranslatorCpp::tQt(
                           "QObject",
                           "Invalid cores setting: %1. Must be between 1 and %2"))
                           .arg(cores)
                           .arg(maxCores);
#else
        qCritical() << QObject::tr("Invalid cores setting: %1. Must be between 1 and %2").arg(cores).arg(maxCores);
#endif
        return false;
    }

    // Check throttle setting
    if (throttle > 20) {
#ifdef CLI_BUILD
        qCritical() << QString::fromStdString(AppTranslatorCpp::tQt(
                           "QObject",
                           "Invalid throttle setting: %1. Must be between 0 and 20"))
                           .arg(throttle);
#else
        qCritical() << QObject::tr("Invalid throttle setting: %1. Must be between 0 and 20").arg(throttle);
#endif
        return false;
    }

    // Check snapshot directory
    if (snapshotDir.isEmpty()) {
#ifdef CLI_BUILD
        qCritical() << QString::fromStdString(AppTranslatorCpp::tQt("QObject", "Snapshot directory cannot be empty"));
#else
        qCritical() << QObject::tr("Snapshot directory cannot be empty");
#endif
        return false;
    }

    // Note: Directory creation is handled later with elevated permissions in checkSnapshotDir()

    // Check snapshot name
    if (snapshotName.isEmpty()) {
#ifdef CLI_BUILD
        qCritical() << QString::fromStdString(AppTranslatorCpp::tQt("QObject", "Snapshot name cannot be empty"));
#else
        qCritical() << QObject::tr("Snapshot name cannot be empty");
#endif
        return false;
    }

    // Check for invalid characters in snapshot name
    bool hasInvalidChar = false;
    for (qsizetype i = 0; i < snapshotName.size(); ++i) {
        const QChar ch = snapshotName.at(i);
        if (ch == '<' || ch == '>' || ch == ':' || ch == '"' || ch == '/' || ch == '\\' || ch == '|' || ch == '?' || ch == '*') {
            hasInvalidChar = true;
            break;
        }
    }
    if (hasInvalidChar) {
#ifdef CLI_BUILD
        qCritical() << QString::fromStdString(AppTranslatorCpp::tQt(
                           "QObject",
                           "Snapshot name contains invalid characters: %1"))
                           .arg(snapshotName);
#else
        qCritical() << QObject::tr("Snapshot name contains invalid characters: %1").arg(snapshotName);
#endif
        return false;
    }

    // Check kernel
    if (kernel.isEmpty()) {
#ifdef CLI_BUILD
        qCritical() << QString::fromStdString(AppTranslatorCpp::tQt("QObject", "Kernel version cannot be empty"));
#else
        qCritical() << QObject::tr("Kernel version cannot be empty");
#endif
        return false;
    }

    if (!FileCpp::exists(("/boot/vmlinuz-" + kernel).toStdString())) {
#ifdef CLI_BUILD
        qCritical() << QString::fromStdString(AppTranslatorCpp::tQt(
                           "QObject",
                           "Kernel file not found: /boot/vmlinuz-%1"))
                           .arg(kernel);
#else
        qCritical() << QObject::tr("Kernel file not found: /boot/vmlinuz-%1").arg(kernel);
#endif
        return false;
    }

    // Check if SQUASHFS is available in kernel
    const std::string configPath = std::string("/boot/config-") + kernel.toStdString();
    std::vector<std::string> grepArgs;
    grepArgs.reserve(3);
    grepArgs.push_back("-q");
    grepArgs.push_back("^CONFIG_SQUASHFS=[ym]");
    grepArgs.push_back(configPath);
    if (ProcessRunner::execute("grep", grepArgs) != 0) {
#ifdef CLI_BUILD
        qCritical() << QString::fromStdString(AppTranslatorCpp::tQt(
                           "QObject",
                           "Kernel %1 doesn't support Squashfs"))
                           .arg(kernel);
#else
        qCritical() << QObject::tr("Kernel %1 doesn't support Squashfs").arg(kernel);
#endif
        return false;
    }

    // Validate exclusions
    if (!validateExclusions()) {
        return false;
    }

#ifdef CLI_BUILD
    // For CLI-only builds, always validate space requirements at startup
    if (!validateSpaceRequirements()) {
        return false;
    }
#else
    // Validate space requirements only when running in CLI
    if (!isGuiApp && !validateSpaceRequirements()) {
        return false;
    }
#endif

    qDebug() << "Configuration validation passed";
    return true;
}

bool Settings::validateExclusions() const
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";

    // Check if exclusion file exists and is readable
    if (!snapshotExcludesPath.isEmpty() && !FileCpp::exists(snapshotExcludesPath.toStdString())) {
#ifdef CLI_BUILD
        qCritical() << QString::fromStdString(AppTranslatorCpp::tQt(
                           "QObject",
                           "Exclusion file does not exist: %1"))
                           .arg(snapshotExcludesPath);
#else
        qCritical() << QObject::tr("Exclusion file does not exist: %1").arg(snapshotExcludesPath);
#endif
        return false;
    }

    // Validate session exclusions format
    if (!sessionExcludes.isEmpty()) {
        // Check for balanced quotes
        const int quoteCount = sessionExcludes.count('"');
        if (quoteCount % 2 != 0) {
#ifdef CLI_BUILD
            qCritical() << QString::fromStdString(AppTranslatorCpp::tQt("QObject", "Unbalanced quotes in exclusion list"));
#else
            qCritical() << QObject::tr("Unbalanced quotes in exclusion list");
#endif
            return false;
        }
    }

    qDebug() << "Exclusion validation passed";
    return true;
}

bool Settings::validateSpaceRequirements() const
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";

    // Check if we have minimum free space (at least 1GB)
    constexpr quint64 MIN_FREE_SPACE = 1024 * 1024; // 1GB in KiB

    // Get free space for snapshot directory (or its parent if it doesn't exist)
    QString pathToCheck = snapshotDir;
    if (!DirCpp::exists(snapshotDir.toStdString())) {
        // If snapshot dir doesn't exist, check parent directory
        pathToCheck = QString::fromLocal8Bit(DirCpp::absolutePathOfContainingDir(snapshotDir.toStdString()).c_str());
    }

    const quint64 availableSpace = static_cast<quint64>(FileSystemUtilsCpp::getFreeSpaceKiB(pathToCheck.toStdString()));
    if (availableSpace < MIN_FREE_SPACE) {
#ifdef CLI_BUILD
        qCritical() << QString::fromStdString(AppTranslatorCpp::tQt(
                           "QObject",
                           "Insufficient free space: %1 KiB available, minimum %2 KiB required"))
                           .arg(availableSpace)
                           .arg(MIN_FREE_SPACE);
#else
        qCritical() << QObject::tr("Insufficient free space: %1 KiB available, minimum %2 KiB required")
                           .arg(availableSpace).arg(MIN_FREE_SPACE);
#endif
        return false;
    }

    // Only check work directory space if it has been initialized (checkTempDir() called)
    if (freeSpaceWork > 0 && freeSpaceWork < MIN_FREE_SPACE) {
#ifdef CLI_BUILD
        qCritical() << QString::fromStdString(AppTranslatorCpp::tQt(
                           "QObject",
                           "Insufficient free space in work directory: %1 KiB available, minimum %2 KiB required"))
                           .arg(freeSpaceWork)
                           .arg(MIN_FREE_SPACE);
#else
        qCritical() << QObject::tr("Insufficient free space in work directory: %1 KiB available, minimum %2 KiB required")
                           .arg(freeSpaceWork).arg(MIN_FREE_SPACE);
#endif
        return false;
    }

    qDebug() << "Space requirements validation passed";
    return true;
}

bool Settings::initializeConfiguration()
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";

    // Validate maxCores
    if (maxCores == 0) {
#ifdef CLI_BUILD
        qCritical() << QString::fromStdString(AppTranslatorCpp::tQt("QObject", "Failed to determine number of CPU cores"));
#else
        qCritical() << QObject::tr("Failed to determine number of CPU cores");
#endif
        return false;
    }

    // Check if config file exists and is readable
    const std::string configPathStd = configFilePath.toStdString();
    if (!FileCpp::exists(configPathStd)) {
#ifdef CLI_BUILD
        qWarning() << QString::fromStdString(AppTranslatorCpp::tQt(
                          "QObject",
                          "Configuration file does not exist: %1"))
                          .arg(configFilePath);
        qWarning() << QString::fromStdString(AppTranslatorCpp::tQt("QObject", "Using default settings"));
#else
        qWarning() << QObject::tr("Configuration file does not exist: %1").arg(configFilePath);
        qWarning() << QObject::tr("Using default settings");
#endif
    } else {
        FileCpp f(configPathStd);
        if (!f.open(FileCpp::OpenMode::ReadOnly)) {
#ifdef CLI_BUILD
            qCritical() << QString::fromStdString(AppTranslatorCpp::tQt(
                           "QObject",
                           "Cannot read configuration file: %1"))
                           .arg(configFilePath);
            qCritical() << QString::fromStdString(AppTranslatorCpp::tQt("QObject", "Error: %1"))
                           .arg(QString::fromLocal8Bit(f.errorString().c_str()));
#else
            qCritical() << QObject::tr("Cannot read configuration file: %1").arg(configFilePath);
            qCritical() << QObject::tr("Error: %1").arg(QString::fromLocal8Bit(f.errorString().c_str()));
#endif
            return false;
        }
        f.close();
    }

    // Check for required system tools
    QStringList requiredTools = {"mksquashfs", "xorriso", "lslogins"};
    for (const QString &tool : requiredTools) {
        if (ProcessRunner::execute("which", {tool.toStdString()}) != 0) {
#ifdef CLI_BUILD
            qCritical() << QString::fromStdString(AppTranslatorCpp::tQt(
                           "QObject",
                           "Required tool not found: %1"))
                           .arg(tool);
#else
            qCritical() << QObject::tr("Required tool not found: %1").arg(tool);
#endif
            return false;
        }
    }

    // Check for required directories
    QStringList requiredDirs = {"/boot", "/etc", "/usr/lib"};
    for (const QString &dir : requiredDirs) {
        if (!DirCpp::exists(dir.toStdString())) {
#ifdef CLI_BUILD
            qCritical() << QString::fromStdString(AppTranslatorCpp::tQt(
                           "QObject",
                           "Required directory not found: %1"))
                           .arg(dir);
#else
            qCritical() << QObject::tr("Required directory not found: %1").arg(dir);
#endif
            return false;
        }
    }

    qDebug() << "Configuration initialization passed";
    return true;
}

void Settings::handleInitializationError(const QString &error) const
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    qDebug() << error;

    // Log to system log if available
    if (FileCpp::exists(std::string("/usr/bin/logger"))) {
        (void)ProcessRunner::execute(
            "logger",
            {"-t", applicationName.toStdString(), ("Settings initialization error: " + error).toStdString()});
    }

    // Show error message appropriate for current mode (GUI or CLI)
#ifdef CLI_BUILD
    const QString title = QString::fromStdString(AppTranslatorCpp::tQt(
        "QObject",
        "Initialization Error"));
    const QString msg = QString::fromStdString(AppTranslatorCpp::tQt(
        "QObject",
        "Failed to initialize application settings:\n\n%1")).arg(error);
    MessageHandlerCpp::showMessage(MessageHandlerCpp::Critical, title.toStdString(), msg.toStdString());
#else
    const QString title = QObject::tr("Initialization Error");
    const QString msg = QObject::tr("Failed to initialize application settings:\n\n%1").arg(error);
    MessageHandler::showMessage(MessageHandler::Critical, title, msg);
#endif
}

QString Settings::getEditor() const
{
    QString editor = guiEditor;
#ifdef UNIT_TESTS
    if (!ut_guiEditorOverride().isEmpty()) {
        editor = ut_guiEditorOverride();
    }
#endif
    if (editor.isEmpty() || WorkCppUtils::findExecutable(editor.toStdString(), path).empty()) {
        const QString defaultEditor = QString::fromStdString(CommandRunner::getOut(
            "xdg-mime query default text/plain", CommandRunner::QuietMode::Yes));
        const QString desktopFile = QString::fromLocal8Bit(
            StandardPathsCpp::locateApplicationsFile(defaultEditor.toStdString()).c_str());
        FileCpp file(desktopFile.toStdString());
        if (file.open(FileCpp::OpenMode::ReadOnly)) {
            while (true) {
                const std::string lineBytes = file.readLine();
                if (lineBytes.empty()) {
                    break;
                }
                QString line = QString::fromUtf8(lineBytes.c_str(), static_cast<qsizetype>(lineBytes.size()));
                if (startsWithLikeQt(line, QLatin1String("Exec="))) {
                    line = removeLikeQt(line, 0, static_cast<qsizetype>(QStringLiteral("Exec=").size()));
                    line.replace(QLatin1String("%u"), QLatin1String(""));
                    line.replace(QLatin1String("%U"), QLatin1String(""));
                    line.replace(QLatin1String("%f"), QLatin1String(""));
                    line.replace(QLatin1String("%F"), QLatin1String(""));
                    line.replace(QLatin1String("%c"), QLatin1String(""));
                    line.replace(QLatin1String("%C"), QLatin1String(""));
                    line.replace(QLatin1String("-b"), QLatin1String(""));
                    editor = trimmedLikeQt(line);
                    break;
                }
            }
            file.close();
        }
        if (editor.isEmpty()) { // Use nano as backup editor
            editor = "nano";
        }
    }

    const bool isRoot = getuid() == 0;
    const bool isEditorThatElevates = endsWithLikeQt(editor, QLatin1String("kate"))
        || endsWithLikeQt(editor, QLatin1String("kwrite"))
        || endsWithLikeQt(editor, QLatin1String("featherpad"))
        || endsWithLikeQt(editor, QLatin1String("code"))
        || endsWithLikeQt(editor, QLatin1String("codium"));
    const bool isCliEditor = editor.contains(QLatin1String("nano"))
        || editor.contains(QLatin1String("vi"))
        || editor.contains(QLatin1String("vim"))
        || editor.contains(QLatin1String("nvim"))
        || editor.contains(QLatin1String("micro"))
        || editor.contains(QLatin1String("emacs"));

    QString elevate = QString::fromStdString(CommandRunner::elevationTool());
    if (isEditorThatElevates && !isRoot) {
        return editor;
    } else if (isRoot && isEditorThatElevates) {
        // Adjust user switch flag based on tool
        if (elevate.contains("sudo")) {
            elevate += " -u $(logname)";
        } else {
            elevate += " --user $(logname)";
        }
    }
    if (isCliEditor) {
        return "x-terminal-emulator -e " + elevate + " " + editor;
    }
    return elevate + " env DISPLAY=$DISPLAY XAUTHORITY=$XAUTHORITY " + editor;
}

// Return the size of the snapshot folder in MiB
QString Settings::getSnapshotSize() const
{
    qint64 totalSize = 0;
    if (FileCpp::exists(snapshotDir.toStdString())) {
        const std::vector<std::string> isoFiles = DirCpp(snapshotDir.toStdString()).entryList({"*.iso"}, DirCpp::EntryType::Files);
        for (const std::string &file : isoFiles) {
            const QString filePath = QString::fromLocal8Bit(DirCpp(snapshotDir.toStdString()).filePath(file).c_str());
            totalSize += static_cast<qint64>(FileCpp::size(filePath.toStdString()));
        }
    }
    return QString::number(totalSize / (1024 * 1024)) + "MiB";
}

// Number of snapshots in snapshot_dir
int Settings::getSnapshotCount() const
{
    if (FileCpp::exists(snapshotDir.toStdString())) {
        const std::vector<std::string> list = DirCpp(snapshotDir.toStdString()).entryInfoList({"*.iso"}, DirCpp::EntryType::Files);
        return static_cast<int>(list.size());
    }
    return 0;
}

// Return the XDG User Directory for each user with different localizations than English
QString Settings::getXdgUserDirs(const QString &folder)
{
    QStringList resultParts;
    resultParts.reserve(18); // For 3 users x 6 folders, not worth getting the number of users on the system

    QStringList usersLocal = users;
#ifdef UNIT_TESTS
    if (!ut_usersOverride().isEmpty()) {
        usersLocal = ut_usersOverride();
    }
#endif

    const static QHash<QString, QString> englishDirs {
        {"DOCUMENTS", "Documents"}, {"DOWNLOAD", "Downloads"}, {"DESKTOP", "Desktop"},
        {"MUSIC", "Music"},         {"PICTURES", "Pictures"},  {"VIDEOS", "Videos"},
    };
    for (const QString &user : std::as_const(usersLocal)) {
        QString dir
            = QString::fromStdString(CommandRunner::getOutAsRoot(
                "runuser", {"-u", user.toStdString(), "--", "/usr/bin/xdg-user-dir", folder.toStdString()},
                CommandRunner::QuietMode::Yes));
        const QStringList lines = dir.split('\n', Qt::SkipEmptyParts);
        dir.clear();
        for (const QString &line : lines) {
            const QString candidate = trimmedLikeQt(line);
            if (startsWithLikeQt(candidate, QLatin1Char('/'))) {
                dir = candidate;
                break;
            }
        }
        if (!dir.isEmpty() && englishDirs.value(folder) != dir.section('/', -1) && dir != "/home/" + user
            && dir != "/home/" + user + '/') {
            if (startsWithLikeQt(dir, QLatin1Char('/'))) {
                dir = removeLikeQt(dir, 0, 1);
            }
            QString exclusion = folder == "DESKTOP" ? "/!(minstall.desktop)" : "/*\" \"" + dir + "/.*";
            dir.append(exclusion);
            resultParts << dir;
        }
    }
    QString result = resultParts.join("\" \"");
    return result.isEmpty() ? QString() : "\" \"" + result;
}

void Settings::selectKernel()
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    const QString prefix = QStringLiteral("/boot/vmlinuz-");
    if (startsWithLikeQt(kernel, prefix)) {
        kernel = removeLikeQt(kernel, 0, prefix.size());
    }
    if (kernel.isEmpty() || !FileCpp::exists(("/boot/vmlinuz-" + kernel).toStdString())) {
        kernel = currentKernel;
        if (!FileCpp::exists(("/boot/vmlinuz-" + kernel).toStdString())) {
            const std::vector<std::string> vmlinuzFilesCpp = DirCpp("/boot").entryList({"vmlinuz-*"}, DirCpp::EntryType::Files, false);
            if (!vmlinuzFilesCpp.empty()) {
                kernel = QString::fromLocal8Bit(vmlinuzFilesCpp.back().c_str());
                const QString prefix2 = QStringLiteral("vmlinuz-");
                if (startsWithLikeQt(kernel, prefix2)) {
                    kernel = removeLikeQt(kernel, 0, prefix2.size());
                }
            }
            if (!FileCpp::exists(("/boot/vmlinuz-" + kernel).toStdString())) {
#ifdef CLI_BUILD
                const QString message = QString::fromStdString(AppTranslatorCpp::tQt(
                    "QObject",
                    "Could not find a usable kernel"));
                const QString details = QString::fromStdString(AppTranslatorCpp::tQt(
                    "QObject",
                    "Searched for kernel files in /boot/ but none were found or accessible."));
                MessageHandlerCpp::showMessage(MessageHandlerCpp::Critical,
                                               AppTranslatorCpp::tQt("QObject", "Error"),
                                               (message + "\n\n" + details).toStdString());
#else
                QString message = QObject::tr("Could not find a usable kernel");
                QString details = QObject::tr("Searched for kernel files in /boot/ but none were found or accessible.");
                MessageHandler::showMessage(MessageHandler::Critical, QObject::tr("Error"), message + "\n\n" + details);
#endif
#ifdef UNIT_TESTS
                throw Settings::UnitTestExit{EXIT_FAILURE};
#else
                exit(EXIT_FAILURE);
#endif
            }
        }
    }
    // Check if SQUASHFS is available
    const std::string configPath = std::string("/boot/config-") + kernel.toStdString();
    std::vector<std::string> grepArgs;
    grepArgs.reserve(3);
    grepArgs.push_back("-q");
    grepArgs.push_back("^CONFIG_SQUASHFS=[ym]");
    grepArgs.push_back(configPath);
    if (ProcessRunner::execute("grep", grepArgs) != 0) {
#ifdef CLI_BUILD
        const QString message = QString::fromStdString(AppTranslatorCpp::tQt(
            "QObject",
            "Current kernel doesn't support Squashfs, cannot continue."));
        MessageHandlerCpp::showMessage(MessageHandlerCpp::Critical,
                                       AppTranslatorCpp::tQt("QObject", "Error"),
                                       message.toStdString());
#else
        QString message = QObject::tr("Current kernel doesn't support Squashfs, cannot continue.");
        MessageHandler::showMessage(MessageHandler::Critical, QObject::tr("Error"), message);
#endif
#ifdef UNIT_TESTS
        throw Settings::UnitTestExit{EXIT_FAILURE};
#else
        exit(EXIT_FAILURE);
#endif
    }
}

void Settings::setVariables()
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";

    try {
        // live and users are now const members initialized in constructor
        if (users.isEmpty()) {
#ifdef CLI_BUILD
            qWarning() << QString::fromStdString(AppTranslatorCpp::tQt("QObject", "No users found in the system"));
#else
            qWarning() << QObject::tr("No users found in the system");
#endif
        }
    } catch (...) {
#ifdef CLI_BUILD
        qCritical() << QString::fromStdString(AppTranslatorCpp::tQt("QObject", "Failed to determine system information"));
#else
        qCritical() << QObject::tr("Failed to determine system information");
#endif
        throw;
    }

    if ( (FileCpp::exists(std::string("/etc/q4os_version"))) || (!FileCpp::exists(std::string("/etc/lsb-release"))) ) {
      QString debian_version1 = QString::fromStdString(CommandRunner::getOut(
          "cat /etc/debian_version | cut -f1 -d'.'", CommandRunner::QuietMode::Yes));
      projectName = "Debian";
      distroVersion = debian_version1;
      fullDistroName = projectName + "-" + distroVersion + "_" + QString(x86 ? QStringLiteral("386") : QStringLiteral("x64"));
      releaseDate = QDate::currentDate().toString(QStringLiteral("MMMM dd, yyyy"));
      if (debian_version1 == "13") {
        codename = "trixie";
      } else if (debian_version1 == "12") {
        codename = "bookworm";
      } else if (debian_version1 == "11") {
        codename = "bullseye";
      } else {
        codename = "bookworm";
      }
      bootOptions = QStringLiteral("quiet splash loglevel=3 systemd.log_color=0 systemd.show_status=1");
      return;
    }

    QString distroVersionFile;
    if (FileCpp::exists(std::string("/etc/mx-version"))) {
        distroVersionFile = "/etc/mx-version";
    } else if (FileCpp::exists(std::string("/etc/antix-version"))) {
        distroVersionFile = "/etc/antix-version";
    }

    if (FileCpp::exists(std::string("/etc/lsb-release"))) {
        projectName = QString::fromStdString(CommandRunner::getOut("grep -oP '(?<=DISTRIB_ID=).*' /etc/lsb-release"));
    } else {
        projectName = QString::fromStdString(CommandRunner::getOut("lsb_release -i | cut -f2"));
    }
    projectName.replace('"', "");
    if (!distroVersionFile.isEmpty()) {
        distroVersion = QString::fromStdString(CommandRunner::getOut(("cut -f1 -d'_' " + distroVersionFile).toStdString()));
        const QString prefixUnderscore = projectName + QLatin1Char('_');
        const QString prefixDash = projectName + QLatin1Char('-');
        if (startsWithLikeQt(distroVersion, prefixUnderscore)) {
            distroVersion = removeLikeQt(distroVersion, 0, prefixUnderscore.size());
        } else if (startsWithLikeQt(distroVersion, prefixDash)) {
            distroVersion = removeLikeQt(distroVersion, 0, prefixDash.size());
        }
    } else {
        distroVersion = QString::fromStdString(CommandRunner::getOut("lsb_release -r | cut -f2"));
    }
    fullDistroName = projectName + "-" + distroVersion + "_" + QString(x86 ? "386" : "x64");
    releaseDate = QDate::currentDate().toString("MMMM dd, yyyy");
    if (FileCpp::exists(std::string("/etc/lsb-release"))) {
        codename = QString::fromStdString(CommandRunner::getOut("grep -oP '(?<=DISTRIB_CODENAME=).*' /etc/lsb-release"));
    } else {
        codename = QString::fromStdString(CommandRunner::getOut("lsb_release -c | cut -f2"));
    }
    codename.replace('"', "");
    bootOptions = monthly ? "quiet splasht nosplash" : QString::fromStdString(SystemInfoCpp::readKernelOpts());
}

QString Settings::getFilename() const
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    QString stampLocal = stamp;
    QString basenameLocal = snapshotBasename;
#ifdef UNIT_TESTS
    if (!ut_stampOverride().isEmpty()) {
        stampLocal = ut_stampOverride();
    }
    if (!ut_snapshotBasenameOverride().isEmpty()) {
        basenameLocal = ut_snapshotBasenameOverride();
    }
#endif

    if (stampLocal == "datetime") {
        return basenameLocal + "-" + QString::fromStdString(DateTimeCpp::nowLocalYmdHm()) + ".iso";
    } else {
        QString name;
        int n = 1;
        do {
            name = basenameLocal + QString::number(n) + ".iso";
            n++;
        } while (FileCpp::exists((snapshotDir + '/' + name).toStdString()));
        return name;
    }
}

quint64 Settings::getLiveRootSpace()
{
    // rootspaceneeded is the size of the linuxfs file * a compression factor + contents of the rootfs, conservative
    // but fast factors are same as used in live-remaster

    // Load some live variables
    const std::string initrdOut = std::string("/live/config/initrd.out");
    QString sqfile_full = QString::fromStdString(QSettingsCpp::nativeGeneralValueString(
        initrdOut, std::string("SQFILE_FULL"), std::string("/live/boot-dev/antiX/linuxfs")));
    QString toram_mp = QString::fromStdString(QSettingsCpp::nativeGeneralValueString(
        initrdOut, std::string("TORAM_MP"), std::string("/live/to-ram")));
    QString sqfile_path = QString::fromStdString(QSettingsCpp::nativeGeneralValueString(
        initrdOut, std::string("SQFILE_PATH"), std::string("antiX")));
    while (startsWithLikeQt(sqfile_path, QLatin1Char('/'))) {
        sqfile_path = removeLikeQt(sqfile_path, 0, 1);
    }
    QString sqfile_name = QString::fromStdString(QSettingsCpp::nativeGeneralValueString(
        initrdOut, std::string("SQFILE_NAME"), std::string("linuxfs")));
    if (!toram_mp.isEmpty() && FileCpp::exists((toram_mp + "/" + sqfile_path + "/" + sqfile_name).toStdString())) {
        sqfile_full = toram_mp + "/" + sqfile_path + "/" + sqfile_name;
    }

    // Get compression factor by reading the linuxfs squasfs file, if available
    QString linuxfs_compression_type
        = QString::fromStdString(CommandRunner::getOut(
            ("dd if=" + sqfile_full + " bs=1 skip=20 count=2 status=none 2>/dev/null |od -An -tdI").toStdString()));
    constexpr quint8 default_factor = 30;
    quint8 c_factor = default_factor;
    // gzip, xz, or lz4
    const QString compressionName = linuxfsCompressionNameFromTypeCode(linuxfs_compression_type);
    if (!compressionName.isEmpty()) {
        c_factor = compressionFactorValue(compressionName);
    } else {
        qWarning() << "Unknown compression type:" << linuxfs_compression_type;
    }
    quint64 rootfs_file_size = 0;
    quint64 linuxfs_file_size = static_cast<quint64>(
        FileSystemUtilsCpp::bytesTotal(std::string("/live/linux/"))) * 100 / c_factor;
    if (FileCpp::exists(std::string("/live/persist-root"))) {
        rootfs_file_size = static_cast<quint64>(
            FileSystemUtilsCpp::bytesTotal(std::string("/live/persist-root/")));
    }

    // Add rootfs file size to the calculated linuxfs file size.  probaby conservative, as rootfs will likely have some
    // overlap with linuxfs
    return linuxfs_file_size + rootfs_file_size;
}

QString Settings::getUsedSpace()
{
    constexpr double factor = 1024 * 1024 * 1024;
#ifdef CLI_BUILD
    QString out = "\n- " + QString::fromStdString(AppTranslatorCpp::tQt("QObject", "Used space on / (root): "));
#else
    QString out = "\n- " + QObject::tr("Used space on / (root): ");
#endif
    if (live) {
        rootSize = getLiveRootSpace();
#ifdef CLI_BUILD
        out += QString::number(static_cast<double>(rootSize) / factor, 'f', 2) + "GiB -- "
               + QString::fromStdString(AppTranslatorCpp::tQt("QObject", "estimated"));
#else
        out += QString::number(static_cast<double>(rootSize) / factor, 'f', 2) + "GiB -- " + QObject::tr("estimated");
#endif
    } else {
        rootSize = static_cast<quint64>(FileSystemUtilsCpp::bytesTotal(std::string("/")))
                   - static_cast<quint64>(FileSystemUtilsCpp::bytesFree(std::string("/")));
        out += QString::number(static_cast<double>(rootSize) / factor, 'f', 2) + "GiB";
    }
    const bool isHomeMount = FileSystemUtilsCpp::isMountPoint(std::string("/home/"));
    if (isHomeMount) {
        homeSize = static_cast<quint64>(FileSystemUtilsCpp::bytesTotal(std::string("/home/")))
                   - static_cast<quint64>(FileSystemUtilsCpp::bytesFree(std::string("/home/")));
#ifdef CLI_BUILD
        out.append("\n- " + QString::fromStdString(AppTranslatorCpp::tQt("QObject", "Used space on /home: "))
                   + QString::number(static_cast<double>(homeSize) / factor, 'f', 2) + "GiB");
#else
        out.append("\n- " + QObject::tr("Used space on /home: ")
                   + QString::number(static_cast<double>(homeSize) / factor, 'f', 2) + "GiB");
#endif
    } else {
        homeSize = 0; // /home on root
    }
    return out;
}



int Settings::getDebianVerNum()
{
    FileCpp file(std::string("/etc/debian_version"));
    QStringList list;
    if (file.open(FileCpp::OpenMode::ReadOnly | FileCpp::OpenMode::Text)) {
        std::string lineBytes = file.readLine();
        if (!lineBytes.empty() && lineBytes.back() == '\n') {
            lineBytes.pop_back();
            if (!lineBytes.empty() && lineBytes.back() == '\r') {
                lineBytes.pop_back();
            }
        }
        const QString line = QString::fromUtf8(lineBytes.c_str(), static_cast<qsizetype>(lineBytes.size()));
        list = line.split('.');
        file.close();
    } else {
        qCritical() << "Could not open /etc/debian_version:" << file.errorString().c_str() << "Assumes Bullseye";
        return Release::Bullseye;
    }
    bool ok = false;
    int ver = list.at(0).toInt(&ok);
    if (ok) {
        return ver;
    } else {
        QString verName = list.at(0).split('/').at(0);
        if (verName == "bullseye") {
            return Release::Bullseye;
        } else if (verName == "bookworm") {
            return Release::Bookworm;
        } else if (verName == "trixie") {
            return Release::Trixie;
        } else if (verName == "forky") {
            return Release::Forky;
        } else if (verName == "duke") {
            return Release::Duke;
        } else {
            qCritical() << "Unknown Debian version:" << ver << "Assumes Bullseye";
            return Release::Bullseye;
        }
    }
}





QString Settings::getFreeSpaceStrings(const QString &path)
{
    constexpr float factor = 1024 * 1024;
    freeSpace = static_cast<quint64>(FileSystemUtilsCpp::getFreeSpaceKiB(path.toStdString()));
    QString out = QString::number(static_cast<double>(freeSpace) / factor, 'f', 2) + "GiB";

#ifdef CLI_BUILD
    qDebug().noquote() << QString("- "
                                  + QString::fromStdString(AppTranslatorCpp::tQt(
                                      "QObject",
                                      "Free space on %1, where snapshot folder is placed: "))
                                        .arg(path)
                                  + out)
#else
    qDebug().noquote() << QString("- " + QObject::tr("Free space on %1, where snapshot folder is placed: ").arg(path)
                                  + out)
#endif
                       << '\n';

#ifdef CLI_BUILD
    qDebug().noquote() << QString::fromStdString(AppTranslatorCpp::tQt(
                              "QObject",
                              "The free space should be sufficient to hold the compressed data from / and /home\n\n"
                              "      If necessary, you can create more available space\n"
                              "      by removing previous snapshots and saved copies:\n"
                              "      %1 snapshots are taking up %2 of disk space.\n"))
                              .arg(QString::number(getSnapshotCount()), getSnapshotSize());
#else
    qDebug().noquote() << QObject::tr(
                              "The free space should be sufficient to hold the compressed data from / and /home\n\n"
                              "      If necessary, you can create more available space\n"
                              "      by removing previous snapshots and saved copies:\n"
                              "      %1 snapshots are taking up %2 of disk space.\n")
                              .arg(QString::number(getSnapshotCount()), getSnapshotSize());
#endif
    return out;
}



void Settings::excludeItem(const QString &item)
{
#ifdef CLI_BUILD
    if (item == QString::fromStdString(AppTranslatorCpp::tQt("QObject", "Desktop"))) {
#else
    if (item == QObject::tr("Desktop")) {
#endif
        excludeDesktop(true);
        return;
    }
#ifdef CLI_BUILD
    if (item == QString::fromStdString(AppTranslatorCpp::tQt("QObject", "Documents"))) {
#else
    if (item == QObject::tr("Documents")) {
#endif
        excludeDocuments(true);
        return;
    }
#ifdef CLI_BUILD
    if (item == QString::fromStdString(AppTranslatorCpp::tQt("QObject", "Downloads"))) {
#else
    if (item == QObject::tr("Downloads")) {
#endif
        excludeDownloads(true);
        return;
    }
#ifdef CLI_BUILD
    if (item == QString::fromStdString(AppTranslatorCpp::tQt("QObject", "Flatpaks"))) {
#else
    if (item == QObject::tr("Flatpaks")) {
#endif
        excludeFlatpaks(true);
        return;
    }
#ifdef CLI_BUILD
    if (item == QString::fromStdString(AppTranslatorCpp::tQt("QObject", "Music"))) {
#else
    if (item == QObject::tr("Music")) {
#endif
        excludeMusic(true);
        return;
    }
#ifdef CLI_BUILD
    if (item == QString::fromStdString(AppTranslatorCpp::tQt("QObject", "Networks"))) {
#else
    if (item == QObject::tr("Networks")) {
#endif
        excludeNetworks(true);
        return;
    }
#ifdef CLI_BUILD
    if (item == QString::fromStdString(AppTranslatorCpp::tQt("QObject", "Pictures"))) {
#else
    if (item == QObject::tr("Pictures")) {
#endif
        excludePictures(true);
        return;
    }
    if (item == QLatin1String("Steam")) {
        excludeSteam(true);
        return;
    }
#ifdef CLI_BUILD
    if (item == QString::fromStdString(AppTranslatorCpp::tQt("QObject", "Videos"))) {
#else
    if (item == QObject::tr("Videos")) {
#endif
        excludeVideos(true);
        return;
    }
    if (item == QLatin1String("VirtualBox")) {
        excludeVirtualBox(true);
        return;
    }
}

void Settings::excludeDesktop(bool exclude)
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    if (exclude) {
        exclusions.setFlag(Exclude::Desktop);
    }
    QString exclusion = "/home/*/Desktop/!(minstall.desktop)" + getXdgUserDirs("DESKTOP");
    addRemoveExclusion(exclude, exclusion);
}

void Settings::excludeDocuments(bool exclude)
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    if (exclude) {
        exclusions.setFlag(Exclude::Documents);
    }
    QString folder {"home/*/Documents/"};
    QString xdg_name {"DOCUMENTS"};
    QString exclusion = folder + "*\" \"" + folder + ".*" + getXdgUserDirs(xdg_name);
    addRemoveExclusion(exclude, exclusion);
}

void Settings::excludeDownloads(bool exclude)
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    if (exclude) {
        exclusions.setFlag(Exclude::Downloads);
    }
    QString folder {"home/*/Downloads/"};
    QString xdg_name {"DOWNLOAD"};
    QString exclusion = folder + "*\" \"" + folder + ".*" + getXdgUserDirs(xdg_name);
    addRemoveExclusion(exclude, exclusion);
}

void Settings::excludeFlatpaks(bool exclude)
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    if (exclude) {
        exclusions.setFlag(Exclude::Flatpaks);
    }
    QString exclusion = "home/*/.local/share/flatpak/*\" \"home/*/.local/share/flatpak/.*\" "
                        "\"var/lib/flatpak/*\" \"var/lib/flatpak/.*\" "
                        "\"home/*/.var/app/*\" \"home/*/.var/app/.*";
    addRemoveExclusion(exclude, exclusion);
}

void Settings::excludeMusic(bool exclude)
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    if (exclude) {
        exclusions.setFlag(Exclude::Music);
    }
    QString folder {"home/*/Music/"};
    QString xdg_name {"MUSIC"};
    QString exclusion = folder + "*\" \"" + folder + ".*" + getXdgUserDirs(xdg_name);
    addRemoveExclusion(exclude, exclusion);
}

void Settings::excludeNetworks(bool exclude)
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    if (exclude) {
        exclusions.setFlag(Exclude::Networks);
    }
    addRemoveExclusion(exclude, QStringLiteral("/etc/NetworkManager/system-connections/*"));
    addRemoveExclusion(exclude, QStringLiteral("/etc/wicd/*"));
    addRemoveExclusion(exclude, QStringLiteral("/var/lib/connman/*"));
}

void Settings::excludePictures(bool exclude)
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    if (exclude) {
        exclusions.setFlag(Exclude::Pictures);
    }
    QString folder {"home/*/Pictures/"};
    QString xdg_name {"PICTURES"};
    QString exclusion = folder + "*\" \"" + folder + ".*" + getXdgUserDirs(xdg_name);
    addRemoveExclusion(exclude, exclusion);
}

void Settings::excludeSteam(bool exclude)
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    if (exclude) {
        exclusions.setFlag(Exclude::Steam);
    }
    addRemoveExclusion(exclude, QStringLiteral("home/*/.steam"));
    addRemoveExclusion(exclude, QStringLiteral("home/*/.local/share/Steam"));
}

void Settings::excludeSwapFile()
{
    FileCpp file((etcBaseDir() + QStringLiteral("/fstab")).toStdString());
    if (!file.open(FileCpp::OpenMode::ReadOnly | FileCpp::OpenMode::Text)) {
        qWarning() << "Failed to open /etc/fstab";
        return;
    }
    const std::vector<std::uint8_t> content = file.readAll();
    const QStringList lines = QString::fromUtf8(reinterpret_cast<const char *>(content.data()),
                                                static_cast<qsizetype>(content.size()))
                                  .split('\n');

    for (const QString &line : lines) {
        QString trimmedLine = trimmedLikeQt(line);
        if (startsWithLikeQt(trimmedLine, QLatin1Char('/')) && !startsWithLikeQt(trimmedLine, QLatin1String("/dev/"))) {
            QStringList parts;
            QString cur;
            cur.reserve(trimmedLine.size());
            for (qsizetype i = 0; i < trimmedLine.size(); ++i) {
                const QChar ch = trimmedLine.at(i);
                if (ch.isSpace()) {
                    parts.push_back(cur);
                    cur.clear();
                    while (i + 1 < trimmedLine.size() && trimmedLine.at(i + 1).isSpace()) {
                        ++i;
                    }
                } else {
                    cur.append(ch);
                }
            }
            parts.push_back(cur);
            if (parts.size() > 3 && parts.at(2) == "swap") {
                addRemoveExclusion(true, removeLikeQt(parts[0], 0, 1));
            }
        }
    }
}

void Settings::excludeVideos(bool exclude)
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    if (exclude) {
        exclusions.setFlag(Exclude::Videos);
    }
    QString folder {"home/*/Videos/"};
    QString xdg_name {"VIDEOS"};
    QString exclusion = folder + "*\" \"" + folder + ".*" + getXdgUserDirs(xdg_name);
    addRemoveExclusion(exclude, exclusion);
}

void Settings::excludeVirtualBox(bool exclude)
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    if (exclude) {
        exclusions.setFlag(Exclude::VirtualBox);
    }
    addRemoveExclusion(exclude, QStringLiteral("home/*/VirtualBox VMs"));
}

// Load settings from config file
void Settings::loadConfig()
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";

    const std::string systemPath = configFilePath.toStdString();
    const std::map<std::string, std::string> systemKv = QSettingsCpp::nativeGeneralAllKeyValues(systemPath);

    // Ensure we use the logged-in user's config location even when running under sudo/root
    const QString configDir = userConfigBaseDir();
    const std::string org = organizationName.toStdString();
    const std::string app = applicationName.toStdString();
    const std::string configDirStd = configDir.toStdString();

    const std::string userPrimaryPath = QSettingsCpp::nativeUserPrimaryFilePathFromBaseDir(configDirStd, org, app);

    const auto userContains = [&](const std::string &key) -> bool {
        return QSettingsCpp::nativeUserContainsKeyFromBaseDir(configDirStd, org, app, key);
    };
    const auto userValue = [&](const std::string &key, const std::string &def) -> std::string {
        return QSettingsCpp::nativeUserValueStringFromBaseDir(configDirStd, org, app, key, def);
    };
    const auto userSet = [&](const std::string &key, const std::string &value) -> void {
        (void)QSettingsCpp::nativeGeneralSetValueString(userPrimaryPath, key, value);
    };

    const QString systemSnapshotExcludes = trimQuotes(QString::fromUtf8(
        QSettingsCpp::iniGeneralValueString(systemPath, std::string("snapshot_excludes"), std::string()).c_str()));
    const bool userConfiguredSnapshotExcludes = userContains(std::string("snapshot_excludes"));

    // Merge system settings into user settings
    for (const auto &it : systemKv) {
        if (!userContains(it.first)) {
            userSet(it.first, it.second);
        }
    }

    sessionExcludes.clear();
    snapshotDir = trimQuotes(QString::fromUtf8(userValue(std::string("snapshot_dir"), std::string("/home/snapshot")).c_str()));
    if (!endsWithLikeQt(snapshotDir, QLatin1String("/snapshot"))) {
        snapshotDir = QString::fromLocal8Bit(DirCpp::cleanPath((snapshotDir + "/snapshot").toStdString()).c_str());
    }
    const QString userConfigDir = QString::fromLocal8Bit(DirCpp::cleanPath((configDir + "/" + organizationName).toStdString()).c_str());
    const QString userExcludesPath =
        QString::fromLocal8Bit(DirCpp::cleanPath((userConfigDir + "/" + applicationName + "-exclude.list").toStdString()).c_str());
    const QString userConfigPath = QString::fromUtf8(userPrimaryPath.c_str());
    const QString systemExcludesPath = QString::fromLocal8Bit(DirCpp::cleanPath(("/etc/" + applicationName + "-exclude.list").toStdString()).c_str());
    QString localPath = QString::fromLocal8Bit(DirCpp::cleanPath(("/usr/local/share/excludes/" + applicationName + "-exclude.list").toStdString()).c_str());
    QString usrPath = QString::fromLocal8Bit(DirCpp::cleanPath(("/usr/share/excludes/" + applicationName + "-exclude.list").toStdString()).c_str());

#ifdef UNIT_TESTS
    const QString utFallback = Settings::ut_fallbackExcludesPathOverride();
    const QString fallbackExcludesPath = !utFallback.isEmpty() ? utFallback
                                                               : (FileCpp::exists(localPath.toStdString()) ? localPath : usrPath);
    const QString utSource = Settings::ut_excludesSourcePathOverride();
    excludesSourcePath = !utSource.isEmpty() ? utSource
                                             : (FileCpp::exists(systemExcludesPath.toStdString()) ? systemExcludesPath : fallbackExcludesPath);
#else
    const QString fallbackExcludesPath = FileCpp::exists(localPath.toStdString()) ? localPath : usrPath;
    excludesSourcePath = FileCpp::exists(systemExcludesPath.toStdString()) ? systemExcludesPath : fallbackExcludesPath;
#endif
    QString configuredExcludesPath = trimQuotes(QString::fromUtf8(userValue(std::string("snapshot_excludes"), userExcludesPath.toStdString()).c_str()));

    if (!userConfiguredSnapshotExcludes || configuredExcludesPath == systemSnapshotExcludes) {
        configuredExcludesPath = userExcludesPath;
        userSet(std::string("snapshot_excludes"), configuredExcludesPath.toStdString());
    }

    const bool usingDefaultUserPath = configuredExcludesPath == userExcludesPath;
    if (usingDefaultUserPath && !FileCpp::exists(userExcludesPath.toStdString())) {
        if (!excludesSourcePath.isEmpty() && FileCpp::exists(excludesSourcePath.toStdString())) {
            (void)DirCpp().mkpath(userConfigDir.toStdString());
            if (FileCpp::copy(excludesSourcePath.toStdString(), userExcludesPath.toStdString())) {
                qDebug() << "Copied exclusion file from" << excludesSourcePath << "to" << userExcludesPath;
                chownFileToLoggedInUser(userExcludesPath);
            } else {
                qWarning() << "Failed to copy exclusion file from" << excludesSourcePath << "to" << userExcludesPath;
            }
        }
    }
    if (!FileCpp::exists(configuredExcludesPath.toStdString())) {
        qDebug() << "Configured snapshot_excludes file not found (" << configuredExcludesPath
                 << "), using fallback path:" << fallbackExcludesPath;
        configuredExcludesPath = fallbackExcludesPath;
    }
    snapshotExcludesPath = configuredExcludesPath;
    chownFileToLoggedInUser(userConfigPath);
    chownFileToLoggedInUser(configuredExcludesPath);
    // snapshotBasename, makeIsohybrid, guiEditor, stamp, forceInstaller are now const members
    makeMd5sum = QString::fromUtf8(userValue(std::string("make_md5sum"), std::string("no")).c_str()) != "no";
    makeSha512sum = QString::fromUtf8(userValue(std::string("make_sha512sum"), std::string("no")).c_str()) != "no";
    compression = trimQuotes(QString::fromUtf8(userValue(std::string("compression"), std::string("zstd")).c_str()));
    mksqOpt = trimQuotes(QString::fromUtf8(userValue(std::string("mksq_opt"), std::string()).c_str()));
    // edit_boot_menu is now const, initialized in constructor
    tempDirParent = trimQuotes(QString::fromUtf8(userValue(std::string("workdir"), std::string()).c_str()));

    const QString coresValueStr = QString::fromUtf8(userValue(std::string("cores"), QString::number(maxCores).toStdString()).c_str());
    uint storedCores = coresValueStr.toUInt();
    if (storedCores == 0 || storedCores > maxCores) {
        qWarning() << QObject::tr("Invalid stored cores setting (%1). Using detected CPU count: %2")
                          .arg(coresValueStr).arg(maxCores);
        storedCores = maxCores;
        userSet(std::string("cores"), QString::number(storedCores).toStdString());
    }
    cores = storedCores;

    const QString throttleValueStr = QString::fromUtf8(userValue(std::string("throttle"), std::string("0")).c_str());
    throttle = throttleValueStr.toUInt();
    resetAccounts = false;
}

void Settings::excludeAll()
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    excludeDesktop(true);
    excludeDocuments(true);
    excludeDownloads(true);
    excludeFlatpaks(true);
    excludeMusic(true);
    excludeNetworks(true);
    excludePictures(true);
    excludeSteam(true);
    excludeVideos(true);
    excludeVirtualBox(true);
}

void Settings::otherExclusions()
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    // Add exclusions snapshot and work dirs
    addRemoveExclusion(true, snapshotDir);
    addRemoveExclusion(true, workDir);

    if (resetAccounts) {
        addRemoveExclusion(true, etcBaseDir() + QStringLiteral("/minstall.conf"));
        // Exclude /etc/localtime if link and timezone not America/New_York
        FileCpp timezoneFile((etcBaseDir() + QStringLiteral("/timezone")).toStdString());
        if (isSymlinkPosix(etcBaseDir() + QStringLiteral("/localtime"))) {
            if (timezoneFile.open(FileCpp::OpenMode::ReadOnly | FileCpp::OpenMode::Text)) {
                std::string lineBytes = timezoneFile.readLine();
                if (!lineBytes.empty() && lineBytes.back() == '\n') {
                    lineBytes.pop_back();
                    if (!lineBytes.empty() && lineBytes.back() == '\r') {
                        lineBytes.pop_back();
                    }
                }
                const QString timezone = QString::fromUtf8(lineBytes.c_str(), static_cast<qsizetype>(lineBytes.size()));
                if (timezone != "America/New_York") {
                    addRemoveExclusion(true, etcBaseDir() + QStringLiteral("/localtime"));
                }
                timezoneFile.close();
            }
        }
    }
    excludeSwapFile();
}

void Settings::processArgs(
#ifdef CLI_BUILD
    const CommandLineParserStd &argParser
#else
    const QCommandLineParser &argParser
#endif
)
{
#ifdef CLI_BUILD
    const auto valueQ = [&](const char *name) { return QString::fromStdString(argParser.value(name)); };
#endif

    shutdown = argParser.isSet("shutdown");
#ifdef CLI_BUILD
    const QString kernelArg = valueQ("kernel");
#else
    const QString kernelArg = argParser.value("kernel");
#endif
    if (!kernelArg.isEmpty()) {
        kernel = kernelArg;
    }
    // preempt is now const, initialized in constructor
#ifdef CLI_BUILD
    if (!valueQ("directory").isEmpty()) {
        const QString directory = valueQ("directory");
#else
    if (!argParser.value("directory").isEmpty()) {
        const QString directory = argParser.value("directory");
#endif
        if (FileCpp::exists(directory.toStdString())) {
            snapshotDir = QString::fromLocal8Bit(DirCpp::absolutePath(directory.toStdString()).c_str()) + "/snapshot";
        } else {
            qWarning() << "Directory does not exist:" << directory;
#ifdef UNIT_TESTS
            throw Settings::UnitTestExit{EXIT_FAILURE};
#else
            exit(EXIT_FAILURE);
#endif
        }
    }

#ifdef CLI_BUILD
    if (!valueQ("workdir").isEmpty()) {
        const QString workdir = valueQ("workdir");
#else
    if (!argParser.value("workdir").isEmpty()) {
        const QString workdir = argParser.value("workdir");
#endif
        if (FileCpp::exists(workdir.toStdString())) {
            tempDirParent = QString::fromLocal8Bit(DirCpp::absolutePath(workdir.toStdString()).c_str());
        } else {
            qWarning() << "Work directory does not exist:" << workdir;
#ifdef UNIT_TESTS
            throw Settings::UnitTestExit{EXIT_FAILURE};
#else
            exit(EXIT_FAILURE);
#endif
        }
    }

#ifdef CLI_BUILD
    if (!valueQ("datafiles-path").isEmpty()) {
        dataFilesPathArg = valueQ("datafiles-path");
    }
#else
    if (!argParser.value("datafiles-path").isEmpty()) {
        dataFilesPathArg = argParser.value("datafiles-path");
    }
#endif

#ifdef CLI_BUILD
    if (!valueQ("templates-path").isEmpty()) {
        templatesPathArg = valueQ("templates-path");
    }
#else
    if (!argParser.value("templates-path").isEmpty()) {
        templatesPathArg = argParser.value("templates-path");
    }
#endif

#ifdef CLI_BUILD
    if (!valueQ("file").isEmpty()) {
        const auto fileArg = valueQ("file");
#else
    if (!argParser.value(QStringLiteral("file")).isEmpty()) {
        const auto fileArg = argParser.value("file");
#endif
        snapshotName = fileArg + (endsWithLikeQt(fileArg, QLatin1String(".iso")) ? QString() : ".iso");
    } else {
        snapshotName = getFilename();
    }
    if (FileCpp::exists((snapshotDir + '/' + snapshotName).toStdString())) {
        QString message
            = QObject::tr("Output file %1 already exists. Please use another file name, or delete the existent file.")
                  .arg(snapshotDir + '/' + snapshotName);
#ifdef CLI_BUILD
        MessageHandlerCpp::showMessage(MessageHandlerCpp::Critical, QObject::tr("Error").toStdString(), message.toStdString());
#else
        MessageHandler::showMessage(MessageHandler::Critical, QObject::tr("Error"), message);
#endif
#ifdef UNIT_TESTS
        throw Settings::UnitTestExit{EXIT_FAILURE};
#else
        exit(EXIT_FAILURE);
#endif
    }
    resetAccounts = argParser.isSet("reset");
    if (resetAccounts) {
        excludeAll();
    }
    if (argParser.isSet("month")) {
        resetAccounts = true;
    }
    if (argParser.isSet("checksums")) {
        makeSha512sum = makeMd5sum = true;
    }
    if (argParser.isSet("month")) {
        makeSha512sum = true;
        makeMd5sum = false;
    }
    if (argParser.isSet("no-checksums")) {
        makeSha512sum = makeMd5sum = false;
    }
#ifdef CLI_BUILD
    if (!valueQ("compression").isEmpty()) {
        compression = valueQ("compression");
    }
    if (!valueQ("compression-level").isEmpty()) {
        mksqOpt = valueQ("compression-level");
    }
    if (!valueQ("cores").isEmpty()) {
#else
    if (!argParser.value("compression").isEmpty()) {
        compression = argParser.value("compression");
    }
    if (!argParser.value("compression-level").isEmpty()) {
        mksqOpt = argParser.value("compression-level");
    }
    if (!argParser.value("cores").isEmpty()) {
#endif
        bool ok {false};
#ifdef CLI_BUILD
        const uint val = valueQ("cores").toUInt(&ok);
#else
        const uint val = argParser.value("cores").toUInt(&ok);
#endif
        if (!ok || val == 0 || val > maxCores) {
#ifdef CLI_BUILD
            qWarning() << "Invalid cores value:" << valueQ("cores")
#else
            qWarning() << "Invalid cores value:" << argParser.value("cores")
#endif
                       << "- must be between 1 and" << maxCores;
            qWarning() << "Using default:" << cores;
        } else {
            cores = val;
        }
    }
#ifdef CLI_BUILD
    if (!valueQ("throttle").isEmpty()) {
#else
    if (!argParser.value("throttle").isEmpty()) {
#endif
        bool ok {false};
#ifdef CLI_BUILD
        const uint val = valueQ("throttle").toUInt(&ok);
#else
        const uint val = argParser.value("throttle").toUInt(&ok);
#endif
        if (!ok || val > 99) {
#ifdef CLI_BUILD
            qWarning() << "Invalid throttle value:" << valueQ("throttle")
#else
            qWarning() << "Invalid throttle value:" << argParser.value("throttle")
#endif
                       << "- must be between 0 and 99";
            qWarning() << "Using default:" << throttle;
        } else {
            throttle = val;
        }
    }
    selectKernel();
}

void Settings::processExclArgs(
#ifdef CLI_BUILD
    const CommandLineParserStd &argParser
#else
    const QCommandLineParser &argParser
#endif
)
{
#ifdef CLI_BUILD
    const auto valuesQ = [&](const char *name) {
        QStringList out;
        const std::vector<std::string> v = argParser.values(name);
        for (const auto &s : v) {
            out << QString::fromStdString(s);
        }
        return out;
    };
#endif

    static const QSet<QString> valid_options {"Desktop",  "Documents", "Downloads", "Flatpaks", "Music",
                                              "Networks", "Pictures",  "Steam",     "Videos",   "VirtualBox"};
    if (argParser.isSet("exclude")) {
#ifdef CLI_BUILD
        const QStringList options = valuesQ("exclude");
#else
        const QStringList options = argParser.values("exclude");
#endif
        for (const QString &option : options) {
            if (valid_options.contains(option)) {
                excludeItem(option);
            } else {
                qWarning() << "Invalid exclude option:" << option
                         << "Please use one of these options" << valid_options.values();
            }
        }
    }
}

void Settings::setMonthlySnapshot(
#ifdef CLI_BUILD
    const CommandLineParserStd &argParser
#else
    const QCommandLineParser &argParser
#endif
)
{
#ifdef CLI_BUILD
    const auto valueQ = [&](const char *name) { return QString::fromStdString(argParser.value(name)); };
#endif
    QString name = "Debian_" + QString(x86 ? "386" : "x64");
#ifdef CLI_BUILD
    if (valueQ("file").isEmpty()) {
#else
    if (argParser.value("file").isEmpty()) {
#endif
        auto month = QDate::currentDate().toString("MMMM");
#ifdef CLI_BUILD
        if (!valueQ("month").isEmpty()) {
            month += "." + valueQ("month");
#else
        if (!argParser.value("month").isEmpty()) {
            month += "." + argParser.value("month");
#endif
        }
        auto suffix = name.section('_', 1, 1);
        if (qgetenv("DESKTOP_SESSION") == "plasma") {
            suffix = suffix.toLower();
        }
        snapshotName = name.section('_', 0, 0) + '_' + month + '_' + suffix + ".iso";
    }
    if (FileCpp::exists((snapshotDir + '/' + snapshotName).toStdString())) {
        QString message
            = QObject::tr("Output file %1 already exists. Please use another file name, or delete the existent file.")
                  .arg(snapshotDir + '/' + snapshotName);
#ifdef CLI_BUILD
        MessageHandlerCpp::showMessage(MessageHandlerCpp::Critical, QObject::tr("Error").toStdString(), message.toStdString());
#else
        MessageHandler::showMessage(MessageHandler::Critical, QObject::tr("Error"), message);
#endif
        exit(EXIT_FAILURE);
    }
#ifdef CLI_BUILD
    if (valueQ("compression").isEmpty()) {
#else
    if (argParser.value("compression").isEmpty()) {
#endif
        compression = "zstd";
    }
    resetAccounts = true;
    bootOptions = "quiet splasht nosplash";
    excludeAll();
}

// Helper functions for const member initialization
QString Settings::getInitialKernel(
#ifdef CLI_BUILD
    const CommandLineParserStd &argParser
#else
    const QCommandLineParser &argParser
#endif
)
{
#ifdef CLI_BUILD
    QString kernelValue = QString::fromStdString(argParser.value("kernel"));
#else
    QString kernelValue = argParser.value("kernel");
#endif

    // Remove path prefix if present
    const QString prefix = QStringLiteral("/boot/vmlinuz-");
    if (startsWithLikeQt(kernelValue, prefix)) {
        kernelValue = removeLikeQt(kernelValue, 0, prefix.size());
    }

    // If no kernel specified or invalid, use current kernel
    if (kernelValue.isEmpty() || !FileCpp::exists(("/boot/vmlinuz-" + kernelValue).toStdString())) {
        kernelValue = currentKernel;

        // If current kernel doesn't exist, find the latest one
        if (!FileCpp::exists(("/boot/vmlinuz-" + kernelValue).toStdString())) {
            const std::vector<std::string> vmlinuzFilesCpp = DirCpp("/boot").entryList({"vmlinuz-*"}, DirCpp::EntryType::Files, false);
            if (!vmlinuzFilesCpp.empty()) {
                kernelValue = QString::fromLocal8Bit(vmlinuzFilesCpp.back().c_str());
                const QString prefix2 = QStringLiteral("vmlinuz-");
                if (startsWithLikeQt(kernelValue, prefix2)) {
                    kernelValue = removeLikeQt(kernelValue, 0, prefix2.size());
                }
            }
        }
    }

    return kernelValue;
}

bool Settings::getEditBootMenuSetting()
{
    const std::string org = organizationName.toStdString();
    const std::string app = applicationName.toStdString();
    if (QSettingsCpp::nativeUserContainsKey(org, app, std::string("edit_boot_menu"))) {
        return QSettingsCpp::nativeUserValueString(org, app, std::string("edit_boot_menu"), std::string("")) != "no";
    }

    const QString systemPath = QStringLiteral("/etc/") + applicationName + QStringLiteral(".conf");
    return QSettingsCpp::iniGeneralValueString(systemPath.toStdString(), std::string("edit_boot_menu"), std::string("no")) != "no";
}

QString Settings::trimQuotes(const QString &value) const
{
    QString trimmed = value;

    // Remove leading and trailing whitespace first
    trimmed = trimmedLikeQt(trimmed);

    // Remove single quotes if they surround the entire value
    if (startsWithLikeQt(trimmed, QLatin1Char('\'')) && endsWithLikeQt(trimmed, QLatin1Char('\'')) && trimmed.length() > 1) {
        trimmed = trimmed.mid(1, trimmed.length() - 2);
    }

    // Remove double quotes if they surround the entire value
    if (startsWithLikeQt(trimmed, QLatin1Char('"')) && endsWithLikeQt(trimmed, QLatin1Char('"')) && trimmed.length() > 1) {
        trimmed = trimmed.mid(1, trimmed.length() - 2);
    }

    return trimmed;
}

Settings::InitialSettings Settings::getInitialSettings() const
{
    const std::string org = organizationName.toStdString();
    const std::string app = applicationName.toStdString();
    const std::string kForceInstaller = std::string("force_installer");
    const std::string kMakeIsohybrid = std::string("make_isohybrid");
    const std::string kGuiEditor = std::string("gui_editor");
    const std::string kSnapshotBasename = std::string("snapshot_basename");
    const std::string kStamp = std::string("stamp");

    const std::string systemPath = std::string("/etc/") + app + std::string(".conf");

    const auto readStringUserThenSystem = [&](const std::string &key, const std::string &def) -> std::string {
        if (QSettingsCpp::nativeUserContainsKey(org, app, key)) {
            return QSettingsCpp::nativeUserValueString(org, app, key, def);
        }
        return QSettingsCpp::iniGeneralValueString(systemPath, key, def);
    };

    InitialSettings s;
    s.live = SystemInfoCpp::isLive();
    s.forceInstaller = QSettingsCpp::variantStringToBoolLikeQt(readStringUserThenSystem(kForceInstaller, std::string("true")));
    s.makeIsohybrid = readStringUserThenSystem(kMakeIsohybrid, std::string("yes")) == "yes";
    s.guiEditor = trimQuotes(QString::fromUtf8(readStringUserThenSystem(kGuiEditor, std::string()).c_str()));
    s.snapshotBasename = trimQuotes(QString::fromUtf8(readStringUserThenSystem(kSnapshotBasename, std::string("snapshot")).c_str()));
    s.stamp = trimQuotes(QString::fromUtf8(readStringUserThenSystem(kStamp, std::string()).c_str()));
    {
        QStringList out;
        for (const std::string &u : SystemInfoCpp::listUsers()) {
            out.append(QString::fromStdString(u));
        }
        s.users = out;
    }
    return s;
}
