#pragma once

#include "settings_cpp.h"

class Settings;

class SettingsQtAdapter
{
public:
    [[nodiscard]] static SettingsCpp fromQt(const Settings &s);
};
