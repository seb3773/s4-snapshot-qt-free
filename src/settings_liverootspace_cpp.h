#pragma once

#include <cstdint>
#include <functional>
#include <string>

class SettingsLiveRootSpaceCpp
{
public:
    struct Callbacks {
        std::function<void(const std::string &text)> warning;
    };

    static std::uint64_t getLiveRootSpaceLikeSettingsQt(const Callbacks &cb);
};
