#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "box_type.h"

struct WorkCppPlanStep
{
    struct Message {
        std::string text;
    };

    struct MessageBox {
        BoxType type {};
        std::string title;
        std::string text;
    };

    struct RunCommandLine {
        std::string command;
        bool quietYes = false;
    };

    struct ProcAsRoot {
        std::string program;
        std::vector<std::string> args;
        bool quietYes = false;
    };

    struct ProcessExecute {
        std::string program;
        std::vector<std::string> args;
        int timeoutMs = -1;
    };

    struct FileCopy {
        std::string source;
        std::string destination;
    };

    struct FileRemove {
        std::string path;
    };

    struct DirRemoveRecursively {
        std::string path;
    };

    struct TempDirRemove {
        std::string debugName;
    };

    struct Abort {
        std::string reason;
    };

    struct Mkpath {
        std::string path;
    };

    struct Chdir {
        std::string path;
    };

    using Payload = std::variant<Message,
                                 MessageBox,
                                 RunCommandLine,
                                 ProcAsRoot,
                                 ProcessExecute,
                                 FileCopy,
                                 FileRemove,
                                 DirRemoveRecursively,
                                 TempDirRemove,
                                 Abort,
                                 Mkpath,
                                 Chdir>;

    Payload payload;
};

struct WorkCppPlan
{
    std::vector<WorkCppPlanStep> steps;
};
