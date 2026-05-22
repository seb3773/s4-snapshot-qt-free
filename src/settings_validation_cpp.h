#pragma once

#include <functional>
#include <string>

#include "settings_cpp.h"

class SettingsValidationCpp
{
public:
    struct Callbacks {
        std::function<void(const std::string &text)> debug;
        std::function<void(const std::string &text)> critical;
    };

    [[nodiscard]] static bool validateExclusionsLikeSettingsQt(const SettingsCpp &settings, const Callbacks &cb);

    [[nodiscard]] static bool checkCompressionLikeSettingsQt(const SettingsCpp &settings);
};
