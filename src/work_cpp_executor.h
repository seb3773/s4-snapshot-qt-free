#pragma once

#include <functional>
#include <string>
#include <vector>

#include "box_type.h"
#include "command_runner.h"
#include "work_cpp_plan.h"

class WorkCppExecutor
{
public:
    struct Callbacks {
        std::function<void(const std::string &text)> message;
        std::function<void(BoxType type, const std::string &title, const std::string &text)> messageBox;
        std::string applicationName;
    };

    struct Result {
        bool aborted = false;
        std::string abortReason;
    };

    [[nodiscard]] static Result run(const WorkCppPlan &plan, const Callbacks &cb);
};
