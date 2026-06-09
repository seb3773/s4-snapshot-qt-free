#pragma once

#include "settings_cpp.h"

class Settings;

class SettingsQtAdapter
{
public:
    [[nodiscard]] static SettingsCpp fromQt(const Settings &s);

    // Overlay GUI runtime fields onto backend settings loaded from config.
    static void overlayRuntimeFromQt(SettingsCpp &out, const Settings &s);
};
