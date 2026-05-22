#pragma once

#include <QString>

#include "work_cpp_plan.h"

class WorkCreateIsoQtPlanOracle
{
public:
    struct SettingsFields {
        QString workDir;
        QString snapshotDir;
        QString compression;
        std::uint32_t cores = 0;
        std::uint32_t throttle = 0;
        QString mksqOpt;
        QString snapshotExcludesPath;
        QString sessionExcludes;
        bool makeIsohybrid = false;
        bool makeMd5sum = false;
        bool makeSha512sum = false;
    };

    struct Env {
        bool useUnbuffer = false;
        QString umaskOut;
        QString applicationName;
        QString elevateTool;
        int debianVerNum = 0;
        QString bindRootPath = QStringLiteral("/.bind-root");
    };

    [[nodiscard]] static WorkCppPlan planCreateIso(const SettingsFields &settings, const QString &filename, const Env &env);
};
