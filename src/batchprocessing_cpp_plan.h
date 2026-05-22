#pragma once

#include <string>
#include <variant>
#include <vector>

#include "work_cpp_plan.h"

struct BatchprocessingCppPlanStep
{
    struct Debug {
        std::string text;
    };

    struct Critical {
        std::string text;
    };

    struct CallWorkPlan {
        std::string stage;
        WorkCppPlan plan;
    };

    struct Abort {
        std::string reason;
    };

    std::variant<Debug, Critical, CallWorkPlan, Abort> payload;
};

struct BatchprocessingCppPlan
{
    std::vector<BatchprocessingCppPlanStep> steps;
};
