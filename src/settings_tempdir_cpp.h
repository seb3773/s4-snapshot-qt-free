#pragma once

#include "settings_cpp.h"
#include "tempdir.h"

#include <functional>
#include <string>

class SettingsTempDirCpp
{
public:
    struct Callbacks {
        std::function<void(const std::string &text)> debug;
        std::function<void(const std::string &text)> critical;
    };

    [[nodiscard]] static bool checkTempDirLikeSettingsQt(SettingsCpp &settings,
                                                        const Callbacks &cb,
                                                        TempDir *tmpOut);
};
