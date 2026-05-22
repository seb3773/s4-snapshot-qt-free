#pragma once

#include <functional>
#include <string>

class SettingsFilenameCpp
{
public:
    struct Callbacks {
        std::function<void(const std::string &text)> debug;
    };

    [[nodiscard]] static std::string getFilenameLikeSettingsQt(const std::string &snapshotDir,
                                                              const std::string &snapshotBasename,
                                                              const std::string &stamp,
                                                              const Callbacks &cb);
};
