#pragma once

#include <functional>
#include <string>

struct SettingsCpp;

class SettingsSnapshotDirCpp
{
public:
    struct Callbacks {
        std::function<void(const std::string &text)> debug;
    };

    static bool checkSnapshotDirLikeSettingsQt(const SettingsCpp &settings, const Callbacks &cb);
};
