#pragma once

#include "settings_cpp.h"

#include <functional>
#include <string>

class SettingsCheckConfigurationCpp
{
public:
    struct Callbacks {
        std::function<void(const std::string &text)> debug;
        std::function<void(const std::string &text)> critical;
    };

    [[nodiscard]] static bool checkConfigurationLikeSettingsQt(const SettingsCpp &settings,
                                                              bool isCliBuild,
                                                              bool isGuiApp,
                                                              const Callbacks &cb);
};
