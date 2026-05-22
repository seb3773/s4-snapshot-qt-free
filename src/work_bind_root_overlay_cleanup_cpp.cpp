#include "work_bind_root_overlay_cleanup_cpp.h"

#include "command_runner.h"

void WorkBindRootOverlayCleanupCpp::cleanupBindRootOverlayLikeQt(const std::string &applicationName,
                                                                const std::string &elevateTool,
                                                                State &st)
{
    if (st.bindRootOverlayBase.empty()) {
        st.bindRootOverlayActive = false;
        st.bindRootPath = "/.bind-root";
        return;
    }

    const std::string snapshotLib = std::string("/usr/lib/") + applicationName + "/snapshot-lib";
    (void)CommandRunner::run(elevateTool + " " + snapshotLib + " cleanup_overlay " + applicationName,
                             CommandRunner::QuietMode::Yes);

    st.bindRootOverlayActive = false;
    st.bindRootOverlayBase.clear();
    st.bindRootPath = "/.bind-root";
}
