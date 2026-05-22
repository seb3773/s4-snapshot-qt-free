#pragma once

#include <string>

#include "batchprocessing_cpp_plan.h"
#include "settings_cpp.h"
#include "work_cpp_planner.h"

class BatchprocessingCppPlanner
{
public:
    struct Env {
        bool checkCompressionOk = true;
        bool checkSnapshotDirOk = true;
        bool checkTempDirOk = true;

        std::string snapshotDir;

        bool editBootMenu = false;
        std::string editorCmd;

        WorkCppPlanner::SetupEnvEnv setupEnvEnv;
        WorkCppPlanner::CopyNewIsoEnv copyNewIsoEnv;
        WorkCppPlanner::CreateIsoEnv createIsoEnv;
    };

    [[nodiscard]] static BatchprocessingCppPlan planOrchestration(const SettingsCpp &settings, const Env &env);
};
