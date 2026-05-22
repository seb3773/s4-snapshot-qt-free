#pragma once

#include <string>
#include <vector>

class SettingsXdgUserDirsCpp
{
public:
    struct Callbacks {
        void (*debugLine)(void *ctx, const std::string &line) = nullptr;
        void *ctx = nullptr;
    };

    [[nodiscard]] static std::string getXdgUserDirsLikeSettingsQt(const std::vector<std::string> &users,
                                                                 const std::string &folder);

    [[nodiscard]] static std::string getXdgUserDirsLikeSettingsQt(const std::vector<std::string> &users,
                                                                 const std::string &folder,
                                                                 Callbacks callbacks);
};
