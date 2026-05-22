#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "settings_cpp.h"
#include "work_cpp_plan.h"

class WorkCppPlanner
{
public:
    struct CreateIsoEnv {
        bool useUnbuffer = false;
        std::string umaskOut;
        std::string applicationName;
        std::string elevateTool;
        int debianVerNum = 0;
        std::string bindRootPath = "/run/iso-snapshot-cli/bind-root-overlay/root";
    };

    [[nodiscard]] static WorkCppPlan planCreateIso(const SettingsCpp &settings,
                                                  const std::string &filename,
                                                  const CreateIsoEnv &env);

    struct CopyNewIsoEnv {
        bool isoTemplateMultiExists = false;
        bool sysvinitInitExists = false;
        bool systemdSystemdExists = false;

        bool initrdReleaseExists = false;
        bool initrdReleaseIsFile = false;
        bool initrdReleaseDestExists = false;

        bool initrd_releaseExists = false;
        bool initrd_releaseIsFile = false;
        bool initrd_releaseDestExists = false;

        bool initrdTempDirValid = true;
        std::string initrdTempDirPath;
        std::string loggedInUserName;

        std::string applicationName;
    };

    [[nodiscard]] static WorkCppPlan planCopyNewIso(const SettingsCpp &settings, const CopyNewIsoEnv &env);

    [[nodiscard]] static WorkCppPlan planSavePackageList(const SettingsCpp &settings, const std::string &fileName);

    [[nodiscard]] static WorkCppPlan planEditBootMenu(const SettingsCpp &settings, const std::string &editorCmd);

    struct SetupEnvEnv {
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

        std::string applicationName;
        std::string elevateTool;

        bool mxVersionFileExistsInUsrLocal = true;
        bool lsbReleaseExistsInUsrLocal = true;

        // cleanUp() planning
        bool cleanUp_started = true;
        bool cleanUp_done = false;
        bool cleanUp_cleanupConfExists = false;
        bool cleanUp_bindRootOverlayBaseNonEmpty = false;
    };

    [[nodiscard]] static WorkCppPlan planSetupEnv(const SettingsCpp &settings, const SetupEnvEnv &env);

    struct CleanupEnv {
        bool started = true;
        bool done = false;
        bool cleanupConfExists = false;
        bool bindRootOverlayBaseNonEmpty = false;
        bool shutdownRequested = false;
        
        std::string applicationName;
        std::string elevateTool;
        std::string snapshotDir;
        std::string snapshotName;
    };

    [[nodiscard]] static WorkCppPlan planCleanup(const SettingsCpp &settings, const CleanupEnv &env);

    [[nodiscard]] static std::vector<std::string> splitShellWordsLikeQt(const std::string &text);
};
