#pragma once

#include <cstdint>
#include <string>

struct SettingsCpp
{
    bool x86 = false;
    std::uint32_t maxCores = 0;
    bool monthly = false;
    bool overrideSize = false;
    bool editBootMenu = false;

    std::uint32_t exclusionsMask = 0;

    bool resetAccounts = false;
    bool forceInstaller = false;
    bool live = false;

    std::string guiEditor;

    std::string workDir;
    std::string snapshotDir;
    std::string snapshotName;
    std::string tempDirParent;

    std::uint64_t freeSpace = 0;
    std::uint64_t freeSpaceWork = 0;

    std::string snapshotExcludesPath;
    std::string sessionExcludes;
    std::string excludesSourcePath;

    bool shutdown = false;

    std::string kernel;
    std::string compression;
    std::uint32_t cores = 0;
    std::uint32_t throttle = 0;
    std::string mksqOpt;

    bool makeIsohybrid = false;
    bool makeMd5sum = false;
    bool makeSha512sum = false;

    std::string bootOptions;

    std::string projectName;
    std::string distroVersion;
    std::string codename;
    std::string fullDistroName;
    std::string releaseDate;
};
