#pragma once

#include <functional>
#include <string>

#include "batchprocessing_cpp_plan.h"
#include "settings_cpp.h"
#include "work_cpp_executor.h"

class BatchprocessingCppRunner
{
public:
    struct Callbacks {
        std::function<void(const std::string &text)> debug;
        std::function<void(const std::string &text)> critical;
    };

    struct Dependencies {
        std::function<WorkCppExecutor::Result(const WorkCppPlan &plan, const WorkCppExecutor::Callbacks &cb)> runWork;
        std::string applicationName;
    };

    struct Result {
        bool aborted = false;
        std::string abortReason;

        // Settings are mutated by the runtime stage (e.g. workDir/tempDirParent/sessionExcludes).
        SettingsCpp settings;
    };

    [[nodiscard]] static Result run(const BatchprocessingCppPlan &plan,
                                   const SettingsCpp &settings,
                                   const Callbacks &cb,
                                   const Dependencies &deps);

    [[nodiscard]] static Result runFromSettings(SettingsCpp settings,
                                               const std::string &applicationName,
                                               const Callbacks &cb,
                                               const Dependencies &deps);
};
