#include "settings_qt_adapter.h"

#include "settings.h"

SettingsCpp SettingsQtAdapter::fromQt(const Settings &s)
{
    SettingsCpp out;

    out.x86 = s.x86;
    out.maxCores = static_cast<std::uint32_t>(s.maxCores);
    out.monthly = s.monthly;
    out.overrideSize = s.overrideSize;
    out.live = s.live;
    out.editBootMenu = s.editBootMenu;

    out.workDir = s.workDir.toStdString();
    out.snapshotDir = s.snapshotDir.toStdString();
    out.snapshotName = s.snapshotName.toStdString();
    out.tempDirParent = s.tempDirParent.toStdString();

    out.freeSpace = static_cast<std::uint64_t>(s.freeSpace);
    out.freeSpaceWork = static_cast<std::uint64_t>(s.freeSpaceWork);

    out.snapshotExcludesPath = s.snapshotExcludesPath.toStdString();
    out.sessionExcludes = s.sessionExcludes.toStdString();
    out.excludesSourcePath = s.getExcludesSourcePath().toStdString();

    out.shutdown = s.shutdown;

    out.kernel = s.kernel.toStdString();
    out.compression = s.compression.toStdString();
    out.cores = static_cast<std::uint32_t>(s.cores);
    out.throttle = static_cast<std::uint32_t>(s.throttle);
    out.mksqOpt = s.mksqOpt.toStdString();

    out.makeIsohybrid = s.makeIsohybrid;
    out.makeMd5sum = s.makeMd5sum;
    out.makeSha512sum = s.makeSha512sum;

    out.bootOptions = s.bootOptions.toStdString();

    out.projectName = s.projectName.toStdString();
    out.distroVersion = s.distroVersion.toStdString();
    out.fullDistroName = s.fullDistroName.toStdString();
    out.codename = s.codename.toStdString();
    out.releaseDate = s.releaseDate.toStdString();

    return out;
}

void SettingsQtAdapter::overlayRuntimeFromQt(SettingsCpp &out, const Settings &s)
{
    const SettingsCpp runtime = fromQt(s);

    if (!runtime.workDir.empty()) {
        out.workDir = runtime.workDir;
    }
    if (!runtime.snapshotDir.empty()) {
        out.snapshotDir = runtime.snapshotDir;
    }
    if (!runtime.snapshotName.empty()) {
        out.snapshotName = runtime.snapshotName;
    }
    if (!runtime.tempDirParent.empty()) {
        out.tempDirParent = runtime.tempDirParent;
    }

    out.freeSpace = runtime.freeSpace;
    out.freeSpaceWork = runtime.freeSpaceWork;

    out.snapshotExcludesPath = runtime.snapshotExcludesPath;
    out.sessionExcludes = runtime.sessionExcludes;
    out.excludesSourcePath = runtime.excludesSourcePath;

    out.shutdown = runtime.shutdown;

    out.x86 = runtime.x86;
    out.live = runtime.live;
    out.editBootMenu = runtime.editBootMenu;

    out.kernel = runtime.kernel;
    out.compression = runtime.compression;
    out.cores = runtime.cores;
    out.throttle = runtime.throttle;
    out.mksqOpt = runtime.mksqOpt;
    out.bootOptions = runtime.bootOptions;

    out.makeIsohybrid = runtime.makeIsohybrid;
    out.makeMd5sum = runtime.makeMd5sum;
    out.makeSha512sum = runtime.makeSha512sum;

    out.projectName = runtime.projectName;
    out.distroVersion = runtime.distroVersion;
    out.fullDistroName = runtime.fullDistroName;
    out.codename = runtime.codename;
    out.releaseDate = runtime.releaseDate;
}
