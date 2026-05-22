#pragma once

#include <functional>
#include <string>
#include <vector>

#include "settings_cpp.h"

class SettingsExclusionsCpp
{
public:
    struct Callbacks
    {
        std::function<void(const std::string &msg)> onWarning;
    };

    static void otherExclusionsLikeSettingsQt(SettingsCpp &settings);
    static void otherExclusionsLikeSettingsQt(SettingsCpp &settings, const Callbacks &callbacks);

    static void excludeDesktopLikeSettingsQt(SettingsCpp &settings, bool exclude, const std::vector<std::string> &users);
    static void excludeDocumentsLikeSettingsQt(SettingsCpp &settings, bool exclude, const std::vector<std::string> &users);
    static void excludeDownloadsLikeSettingsQt(SettingsCpp &settings, bool exclude, const std::vector<std::string> &users);
    static void excludeMusicLikeSettingsQt(SettingsCpp &settings, bool exclude, const std::vector<std::string> &users);
    static void excludePicturesLikeSettingsQt(SettingsCpp &settings, bool exclude, const std::vector<std::string> &users);
    static void excludeVideosLikeSettingsQt(SettingsCpp &settings, bool exclude, const std::vector<std::string> &users);

    static void excludeFlatpaksLikeSettingsQt(SettingsCpp &settings, bool exclude);

    static void excludeNetworksLikeSettingsQt(SettingsCpp &settings, bool exclude);
    static void excludeSteamLikeSettingsQt(SettingsCpp &settings, bool exclude);
    static void excludeVirtualBoxLikeSettingsQt(SettingsCpp &settings, bool exclude);

    static void excludeAllLikeSettingsQt(SettingsCpp &settings, const std::vector<std::string> &users);

    static void excludeSwapFileLikeSettingsQt(SettingsCpp &settings, const Callbacks &callbacks);
};
