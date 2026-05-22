#pragma once

#include <QString>

#include "work_cpp_plan.h"

class WorkSetupEnvQtPlanOracle
{
public:
    struct SettingsFields {
        QString workDir;
        bool forceInstaller = false;
        bool resetAccounts = false;

        QString projectName;
        QString distroVersion;
        QString codename;
        QString fullDistroName;
        QString releaseDate;
    };

    struct Env {
        bool workDirContainsS4Snapshot = true;
        bool bootIsMountpoint = false;

        bool bindRootOverlayActive = false;

        bool needInstallCalamares = false;

        bool setupBindRootOverlayOk = true;

        // setupBindRootOverlay internals
        bool setupBindRootOverlay_bindRootIsMountpoint = false;
        bool setupBindRootOverlay_lowerIsMountpoint = false;
        bool setupBindRootOverlay_bindMountOk = true;
        bool setupBindRootOverlay_overlayMountOk = true;

        QString applicationName;
        QString elevateTool;

        bool mxVersionFileExistsInUsrLocal = true;
        bool lsbReleaseExistsInUsrLocal = true;

        // cleanUp() planning
        bool cleanUp_started = true;
        bool cleanUp_done = false;
        bool cleanUp_cleanupConfExists = false;
        bool cleanUp_bindRootOverlayBaseNonEmpty = false;
    };

    [[nodiscard]] static WorkCppPlan planSetupEnv(const SettingsFields &settings, const Env &env);
};
