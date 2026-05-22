#pragma once

#include <functional>
#include <string>

class SettingsDebianVerNumCpp
{
public:
    struct Callbacks {
        std::function<void(const std::string &text)> critical;
    };

    static int getDebianVerNumLikeSettingsQt(const Callbacks &cb);

    static int parseDebianVersionLineLikeSettingsQt(const std::string &lineUtf8, const Callbacks &cb);
};
