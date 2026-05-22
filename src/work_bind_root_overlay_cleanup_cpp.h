#pragma once

#include <string>

class WorkBindRootOverlayCleanupCpp
{
public:
    struct State {
        bool bindRootOverlayActive = false;
        std::string bindRootOverlayBase;
        std::string bindRootPath = "/.bind-root";
    };

    static void cleanupBindRootOverlayLikeQt(const std::string &applicationName,
                                            const std::string &elevateTool,
                                            State &st);
};
