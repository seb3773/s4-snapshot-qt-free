#include "settings_qt_adapter.h"

#include "settings.h"

SettingsCpp SettingsQtAdapter::fromQt(const Settings &s)
{
    SettingsCpp out;

    out.x86 = s.x86;
    out.maxCores = static_cast<std::uint32_t>(s.maxCores);
    out.monthly = s.monthly;
    out.overrideSize = s.overrideSize;
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

    return out;
}
