#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "settings_cpp.h"

class SettingsProcessArgsCpp
{
public:
    struct Callbacks
    {
        std::function<void(const std::string &msg)> onWarning;
        std::function<void(const std::string &title, const std::string &msg)> onCriticalMessage;
    };

    struct Input
    {
        std::string currentKernel;
        std::vector<std::string> users;

        bool shutdownSet = false;
        bool resetSet = false;
        bool monthSet = false;
        bool checksumsSet = false;
        bool noChecksumsSet = false;

        std::string kernelArg;
        std::string directoryArg;
        std::string workdirArg;
        std::string fileArg;
        std::string compressionArg;
        std::string compressionLevelArg;
        std::string coresArg;
        std::string throttleArg;
        std::string dataFilesPathArg;
        std::string templatesPathArg;

        // Used when fileArg is empty.
        std::string defaultSnapshotName;
    };

#ifdef UNIT_TESTS
    struct UnitTestExit
    {
        int exitCode = 0;
    };
#endif

    static void applyLikeSettingsQt(SettingsCpp &settings, const Input &in, const Callbacks &callbacks);
};
