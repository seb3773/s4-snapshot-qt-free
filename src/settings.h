/**********************************************************************
 *  settings.h
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
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <QScopedPointer>
#include <QString>
#include <QStringList>
#ifdef CLI_BUILD
#include "command_line_parser_std.h"
#else
#include <QCommandLineParser>
#endif

#include "tempdir.h"

extern QString currentKernel;

namespace Release
{
enum Version { Jessie = 8, Stretch, Buster, Bullseye, Bookworm, Trixie, Forky, Duke };
}

class Settings
{
public:
#ifdef UNIT_TESTS
    struct UnitTestExit {
        int exitCode = 0;
    };
#endif
#ifdef CLI_BUILD
    explicit Settings(const CommandLineParserStd &argParser,
                      bool isGuiApp,
                      const QString &applicationName,
                      const QString &organizationName);
#else
    explicit Settings(const QCommandLineParser &argParser,
                      bool isGuiApp,
                      const QString &applicationName,
                      const QString &organizationName);
#endif

#ifdef UNIT_TESTS
    Settings();

    static void ut_setUserConfigBaseDirOverride(const QString &dir);
    static QString ut_userConfigBaseDirOverride();

    void ut_setConfigFilePath(const QString &path);

    static void ut_setExcludesSourcePathOverride(const QString &path);
    static QString ut_excludesSourcePathOverride();

    static void ut_setFallbackExcludesPathOverride(const QString &path);
    static QString ut_fallbackExcludesPathOverride();

    static void ut_setEtcDirOverride(const QString &dir);
    static QString ut_etcDirOverride();

    static void ut_setGuiEditorOverride(const QString &editor);
    static QString ut_guiEditorOverride();

    static void ut_setSnapshotBasenameOverride(const QString &basename);
    static QString ut_snapshotBasenameOverride();

    static void ut_setStampOverride(const QString &stamp);
    static QString ut_stampOverride();

    static void ut_setUsersOverride(const QStringList &users);
    static QStringList ut_usersOverride();
#endif

    [[nodiscard]] QString getEditor() const;
    [[nodiscard]] QString getFilename() const;
    [[nodiscard]] QString getFreeSpaceStrings(const QString &path);
    [[nodiscard]] QString getSnapshotSize() const;
    [[nodiscard]] QString getUsedSpace();
    [[nodiscard]] QString getXdgUserDirs(const QString &folder);
    [[nodiscard]] bool checkCompression() const;
    [[nodiscard]] bool checkConfiguration() const;
    [[nodiscard]] bool checkSnapshotDir() const;
    [[nodiscard]] QString getExcludesSourcePath() const { return excludesSourcePath; }
    [[nodiscard]] bool checkTempDir();
    [[nodiscard]] bool initializeConfiguration();
    [[nodiscard]] bool validateExclusions() const;
    [[nodiscard]] bool validateSpaceRequirements() const;
    [[nodiscard]] int getSnapshotCount() const;
    [[nodiscard]] quint64 getLiveRootSpace();
    [[nodiscard]] static int getDebianVerNum();
    void addRemoveExclusion(bool add, QString exclusion);
    void excludeAll();
    void excludeDesktop(bool exclude);
    void excludeDocuments(bool exclude);
    void excludeDownloads(bool exclude);
    void excludeFlatpaks(bool exclude);
    void excludeItem(const QString &item);
    void excludeMusic(bool exclude);
    void excludeNetworks(bool exclude);
    void excludePictures(bool exclude);
    void excludeSteam(bool exclude);
    void excludeSwapFile();
    void excludeVideos(bool exclude);
    void excludeVirtualBox(bool exclude);
    void handleInitializationError(const QString &error) const;
    void loadConfig();
    void otherExclusions();
    void processArgs(
#ifdef CLI_BUILD
        const CommandLineParserStd &argParser
#else
        const QCommandLineParser &argParser
#endif
    );
    void processExclArgs(
#ifdef CLI_BUILD
        const CommandLineParserStd &argParser
#else
        const QCommandLineParser &argParser
#endif
    );
    void selectKernel();
    void setMonthlySnapshot(
#ifdef CLI_BUILD
        const CommandLineParserStd &argParser
#else
        const QCommandLineParser &argParser
#endif
    );
    void setVariables();

    [[nodiscard]] quint8 compressionFactorValue(const QString &compressionName) const;

    // Public enums and types
    enum class Exclude {
        Desktop = 1 << 0,
        Documents = 1 << 1,
        Downloads = 1 << 2,
        Flatpaks = 1 << 3,
        Music = 1 << 4,
        Networks = 1 << 5,
        Pictures = 1 << 6,
        Steam = 1 << 7,
        Videos = 1 << 8,
        VirtualBox = 1 << 9
    };
    Q_DECLARE_FLAGS(Exclusions, Exclude)

    const QString applicationName;
    const QString organizationName;

    // Phase 1: Immutable system configuration (const)
    const bool x86;
    const uint maxCores;
    const bool monthly;
    const bool overrideSize;
    const bool editBootMenu;
    const bool isGuiApp;
    static constexpr std::array<std::pair<const char *, std::uint8_t>, 6> compressionFactorTable = {{
        {"xz", 31},
        {"zstd", 35},
        {"gzip", 37},
        {"lzo", 52},
        {"lzma", 52},
        {"lz4", 52},
    }};

    // Phase 2: Mutable UI preferences
    Exclusions exclusions;
    QString bootOptions;
    QString codename;
    QString compression;
    QString distroVersion;
    QString fullDistroName;
    QString kernel;
    QString projectName;
    QString releaseDate;
    bool makeMd5sum {};
    bool makeSha512sum {};
    bool resetAccounts {};
    uint cores {};
    uint throttle {};

    // Phase 3: Runtime state
    QString snapshotExcludesPath;
    QScopedPointer<TempDir> tmpdir;
    QString mksqOpt;
    QString sessionExcludes;
    QString snapshotDir;
    QString snapshotName;
    QString tempDirParent;
    QString workDir;
    bool preempt {};
    bool shutdown {};
    const bool forceInstaller;
    const bool live;
    const bool makeIsohybrid;
    quint64 freeSpace {};
    quint64 freeSpaceWork {};
    QString excludesSourcePath;

private:
    QString configFilePath;
    QString saveMessage;
    QString version;
    const QString guiEditor;
    const QString snapshotBasename;
    const QString stamp;
    const std::vector<std::string> path;
    const QStringList users; // list of users with /home folders
    quint64 homeSize {};
    quint64 rootSize {};

    // Helper functions for const member initialization
    QString getInitialKernel(
#ifdef CLI_BUILD
        const CommandLineParserStd &argParser
#else
        const QCommandLineParser &argParser
#endif
    );
    bool getEditBootMenuSetting();
    QString trimQuotes(const QString &value) const;

    struct InitialSettings {
        bool live;
        bool forceInstaller;
        bool makeIsohybrid;
        QString guiEditor;
        QString snapshotBasename;
        QString stamp;
        QStringList users;
    };

    InitialSettings getInitialSettings() const;
};
