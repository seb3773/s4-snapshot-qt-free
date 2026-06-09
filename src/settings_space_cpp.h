#pragma once

#include "settings_cpp.h"
#include "work_space_cpp.h"

#include <functional>
#include <string>

class SettingsSpaceCpp
{
public:
    struct Callbacks {
        std::function<void(const std::string &text)> debug;
        std::function<void(const std::string &text)> critical;
    };

    static int getSnapshotCountLikeSettingsQt(const SettingsCpp &settings);
    static std::string getSnapshotSizeLikeSettingsQt(const SettingsCpp &settings);

    static std::string getUsedSpaceLikeSettingsQt(const SettingsCpp &settings);

    [[nodiscard]] static bool validateSpaceRequirementsLikeSettingsQt(const SettingsCpp &settings,
                                                                      const Callbacks &cb);

    static std::string getFreeSpaceStringsLikeSettingsQt(SettingsCpp &settings,
                                                         const std::string &path,
                                                         const Callbacks &cb);

    [[nodiscard]] static std::string formatRequiredSpaceEstimateDebugLikeSettingsQt(
        const WorkSpaceCpp::RequiredSpaceEstimate &estimate,
        std::uint64_t freeSpaceKiB);

    [[nodiscard]] static WorkSpaceCpp::RequiredSpaceEstimate getRequiredSpaceEstimateLikeSettingsQt(
        const SettingsCpp &settings,
        const std::string &applicationName,
        const Callbacks &cb);
};
