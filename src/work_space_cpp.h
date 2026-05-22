#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "settings_cpp.h"

class WorkSpaceCpp
{
public:
    struct Callbacks {
        std::function<void(const std::string &text)> message;
        std::function<void(const std::string &text)> warning;
    };

    struct CheckEnoughSpaceResult {
        bool ok = true;
        std::string messageBoxTitle;
        std::string messageBoxText;

        bool shouldMoveWorkDir = false;
        std::string moveWorkDirTo;

        std::uint64_t requiredSpaceKiB = 0;
    };

    [[nodiscard]] static std::uint64_t getRequiredSpaceLikeQt(const SettingsCpp &settings,
                                                             const std::string &applicationName,
                                                             const Callbacks &cb);

    [[nodiscard]] static CheckEnoughSpaceResult checkEnoughSpaceLikeQt(const SettingsCpp &settings,
                                                                       const std::string &applicationName,
                                                                       const Callbacks &cb);
};
