#pragma once

#include <functional>
#include <string>

class WorkBindRootOverlayCpp
{
public:
    struct Callbacks {
        std::function<void(const std::string &text)> warning;
    };

    struct Result {
        bool ok = false;
        bool bindRootOverlayActive = false;
        std::string bindRootOverlayBase;
        std::string bindRootPath;
    };

    [[nodiscard]] static Result setupBindRootOverlayLikeQt(const std::string &applicationName, const Callbacks &cb);
};
