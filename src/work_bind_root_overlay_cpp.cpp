#include "work_bind_root_overlay_cpp.h"

#include "command_runner.h"

namespace {

static void cb_warning(const WorkBindRootOverlayCpp::Callbacks &cb, const std::string &text)
{
    if (cb.warning) {
        cb.warning(text);
    }
}

} // namespace

WorkBindRootOverlayCpp::Result WorkBindRootOverlayCpp::setupBindRootOverlayLikeQt(const std::string &applicationName,
                                                                                 const Callbacks &cb)
{
    Result out;

    const std::string overlayBase = std::string("/run/") + applicationName + "/bind-root-overlay";
    const std::string lowerDir = overlayBase + "/lower";
    const std::string upperDir = overlayBase + "/upper";
    const std::string workDir = overlayBase + "/work";
    const std::string bindRoot = overlayBase + "/root";

    out.bindRootOverlayActive = false;
    out.bindRootOverlayBase.clear();
    out.bindRootPath = "/.bind-root";

    (void)CommandRunner::procAsRoot("mkdir",
                                   {"-p", overlayBase, lowerDir, upperDir, workDir, bindRoot},
                                   std::string(),
                                   CommandRunner::QuietMode::Yes);

    {
        const CommandRunner::Result mp = CommandRunner::procAsRoot(
            "mountpoint", {"-q", bindRoot}, std::string(), CommandRunner::QuietMode::Yes);
        if (mp.started && mp.normalExit && mp.exitCode == 0) {
            (void)CommandRunner::procAsRoot("umount",
                                           {"--recursive", bindRoot},
                                           std::string(),
                                           CommandRunner::QuietMode::Yes);
        }
    }

    {
        const CommandRunner::Result mp = CommandRunner::procAsRoot(
            "mountpoint", {"-q", lowerDir}, std::string(), CommandRunner::QuietMode::Yes);
        if (mp.started && mp.normalExit && mp.exitCode == 0) {
            (void)CommandRunner::procAsRoot("umount",
                                           {"--recursive", lowerDir},
                                           std::string(),
                                           CommandRunner::QuietMode::Yes);
        }
    }

    {
        const CommandRunner::Result m = CommandRunner::procAsRoot(
            "mount", {"--bind", "/", lowerDir}, std::string(), CommandRunner::QuietMode::Yes);
        if (!(m.started && m.normalExit && m.exitCode == 0)) {
            cb_warning(cb, std::string("Failed to bind mount / to ") + lowerDir);
            out.ok = false;
            return out;
        }
    }

    const std::string overlayOptions = std::string("lowerdir=") + lowerDir + ",upperdir=" + upperDir + ",workdir=" + workDir;
    {
        const CommandRunner::Result m = CommandRunner::procAsRoot(
            "mount",
            {"-t", "overlay", "overlay", "-o", overlayOptions, bindRoot},
            std::string(),
            CommandRunner::QuietMode::Yes);
        if (!(m.started && m.normalExit && m.exitCode == 0)) {
            cb_warning(cb, std::string("Failed to mount overlay at ") + bindRoot);
            (void)CommandRunner::procAsRoot("umount",
                                           {"--recursive", lowerDir},
                                           std::string(),
                                           CommandRunner::QuietMode::Yes);
            out.ok = false;
            return out;
        }
    }

    out.ok = true;
    out.bindRootPath = bindRoot;
    out.bindRootOverlayBase = overlayBase;
    out.bindRootOverlayActive = true;
    return out;
}
