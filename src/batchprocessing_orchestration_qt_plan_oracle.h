#pragma once

#include <QString>

#include "batchprocessing_cpp_plan.h"
#include "work_createiso_qt_plan_oracle.h"
#include "work_cpp_plan.h"
#include "work_cpp_planner.h"

class BatchprocessingOrchestrationQtPlanOracle
{
public:
    struct SettingsFields {
        bool monthly = false;
        bool overrideSize = false;
        bool editBootMenu = false;

        QString snapshotName;
        QString workDir;

        // Work setupEnv metadata
        bool forceInstaller = false;
        bool resetAccounts = false;

        QString projectName;
        QString distroVersion;
        QString codename;
        QString fullDistroName;
        QString releaseDate;

        // Work copyNewIso/createIso core fields
        QString kernel;
        QString compression;
        uint cores = 0;
        uint throttle = 0;
        QString mksqOpt;
        bool makeIsohybrid = false;
        bool makeMd5sum = false;
        bool makeSha512sum = false;
        QString bootOptions;
    };

    struct Env {
        bool checkCompressionOk = true;
        bool checkSnapshotDirOk = true;
        bool checkTempDirOk = true;

        bool editBootMenu = false;
        QString editorCmd;

        // Work envs
        bool setupEnv_workDirContainsS4Snapshot = true;
        bool setupEnv_bootIsMountpoint = false;
        bool setupEnv_bindRootOverlayActive = false;
        bool setupEnv_needInstallCalamares = false;
        bool setupEnv_setupBindRootOverlayOk = true;
        bool setupEnv_setupBindRootOverlay_bindRootIsMountpoint = false;
        bool setupEnv_setupBindRootOverlay_lowerIsMountpoint = false;
        bool setupEnv_setupBindRootOverlay_bindMountOk = true;
        bool setupEnv_setupBindRootOverlay_overlayMountOk = true;
        QString setupEnv_applicationName;
        QString setupEnv_elevateTool;
        bool setupEnv_mxVersionFileExistsInUsrLocal = true;
        bool setupEnv_lsbReleaseExistsInUsrLocal = true;

        bool setupEnv_cleanUp_started = true;
        bool setupEnv_cleanUp_done = false;
        bool setupEnv_cleanUp_cleanupConfExists = false;
        bool setupEnv_cleanUp_bindRootOverlayBaseNonEmpty = false;

        bool copyNewIso_isoTemplateMultiExists = true;
        bool copyNewIso_sysvinitInitExists = true;
        bool copyNewIso_systemdSystemdExists = true;
        bool copyNewIso_initrdTempDirValid = true;
        QString copyNewIso_initrdTempDirPath;
        QString copyNewIso_loggedInUserName;
        QString copyNewIso_applicationName;

        WorkCreateIsoQtPlanOracle::Env createIsoEnv;
    };

    [[nodiscard]] static BatchprocessingCppPlan planOrchestration(const SettingsFields &settings, const Env &env);
};
