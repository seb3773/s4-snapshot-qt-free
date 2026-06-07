#include "work_bind_root_overlay_cleanup_cpp.h"

#include "command_runner.h"

void WorkBindRootOverlayCleanupCpp::cleanupBindRootOverlayLikeQt(const std::string &applicationName,
                                                                const std::string &elevateTool,
                                                                State &st)
{
    (void)elevateTool;

    if (st.bindRootOverlayBase.empty()) {
        st.bindRootOverlayActive = false;
        st.bindRootPath = "/.bind-root";
        return;
    }

    (void)CommandRunner::procAsRoot("cleanup_overlay",
                                    {applicationName},
                                    std::string(),
                                    CommandRunner::QuietMode::Yes);

    st.bindRootOverlayActive = false;
    st.bindRootOverlayBase.clear();
    st.bindRootPath = "/.bind-root";
}
