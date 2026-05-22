#pragma once

#include <QString>

#include "work_cpp_plan.h"

class WorkCleanupQtPlanOracle
{
public:
    struct SettingsFields {
        QString snapshotDir;
        QString snapshotName;
        bool shutdown = false;
    };

    struct Env {
        bool started = true;
        bool done = false;
        bool cleanupConfExists = false;
        bool bindRootOverlayBaseNonEmpty = false;
        
        QString applicationName;
        QString elevateTool;
    };

    [[nodiscard]] static WorkCppPlan planCleanup(const SettingsFields &settings, const Env &env);
};

// Made with Bob
