#include "work_setupenv_qt_plan_oracle.h"

#include <stdexcept>

#include "work_qt_oracle.h"
#include "work_cpp_utils.h"

namespace {

static void plan_message_box(WorkCppPlan &p, BoxType t, const std::string &title, const std::string &text)
{
    WorkCppPlanStep st;
    st.payload = WorkCppPlanStep::MessageBox{t, title, text};
    p.steps.push_back(std::move(st));
}

static void plan_run_cmd(WorkCppPlan &p, const std::string &cmd, bool quietYes)
{
    WorkCppPlanStep st;
    st.payload = WorkCppPlanStep::RunCommandLine{cmd, quietYes};
    p.steps.push_back(std::move(st));
}

static void plan_proc_root(WorkCppPlan &p,
                           const std::string &program,
                           const std::vector<std::string> &args,
                           bool quietYes)
{
    WorkCppPlanStep st;
    st.payload = WorkCppPlanStep::ProcAsRoot{program, args, quietYes};
    p.steps.push_back(std::move(st));
}

static void plan_process_execute(WorkCppPlan &p, const std::string &program, const std::vector<std::string> &args, int timeoutMs)
{
    WorkCppPlanStep st;
    st.payload = WorkCppPlanStep::ProcessExecute{program, args, timeoutMs};
    p.steps.push_back(std::move(st));
}

static void plan_file_remove(WorkCppPlan &p, const std::string &path)
{
    WorkCppPlanStep st;
    st.payload = WorkCppPlanStep::FileRemove{path};
    p.steps.push_back(std::move(st));
}

static void plan_message(WorkCppPlan &p, const std::string &text)
{
    WorkCppPlanStep st;
    st.payload = WorkCppPlanStep::Message{text};
    p.steps.push_back(std::move(st));
}

static void plan_temp_dir_remove(WorkCppPlan &p, const std::string &debugName)
{
    WorkCppPlanStep st;
    st.payload = WorkCppPlanStep::TempDirRemove{debugName};
    p.steps.push_back(std::move(st));
}

static void plan_abort(WorkCppPlan &p, const std::string &reason)
{
    WorkCppPlanStep st;
    st.payload = WorkCppPlanStep::Abort{reason};
    p.steps.push_back(std::move(st));
}

static void plan_clean_up(WorkCppPlan &p,
                          const WorkSetupEnvQtPlanOracle::Env &env,
                          const std::string &reason)
{
    const QString snapshotLib = QStringLiteral("/usr/lib/") + env.applicationName + QStringLiteral("/snapshot-lib");
    plan_run_cmd(p, (env.elevateTool + QStringLiteral(" ") + snapshotLib + QStringLiteral(" chown_conf")).toStdString(), true);

    if (!env.cleanUp_started) {
        plan_temp_dir_remove(p, "initrd_dir");
        plan_run_cmd(p,
                     (env.elevateTool + QStringLiteral(" ") + snapshotLib + QStringLiteral(" cleanup_overlay ") + env.applicationName)
                         .toStdString(),
                     true);
        plan_abort(p, std::string("cleanUp exit: ") + reason);
        return;
    }

    plan_message(p, QObject::tr("Cleaning...").toStdString());
    plan_run_cmd(p, (env.elevateTool + QStringLiteral(" ") + snapshotLib + QStringLiteral(" kill_mksquashfs")).toStdString(), true);
    plan_process_execute(p, "sync", {}, -1);

    {
        WorkCppPlanStep st;
        st.payload = WorkCppPlanStep::Chdir{"/"};
        p.steps.push_back(std::move(st));
    }

    if (env.cleanUp_cleanupConfExists) {
        plan_run_cmd(p, (env.elevateTool + QStringLiteral(" ") + snapshotLib + QStringLiteral(" cleanup")).toStdString(), false);
    }

    if (env.cleanUp_bindRootOverlayBaseNonEmpty) {
        plan_run_cmd(p,
                     (env.elevateTool + QStringLiteral(" ") + snapshotLib + QStringLiteral(" cleanup_overlay ") + env.applicationName)
                         .toStdString(),
                     true);
    }
    plan_file_remove(p, "/var/lib/mxdebian/.mxsnapshot_accounts_reset.stp");
    plan_temp_dir_remove(p, "initrd_dir");

    if (env.cleanUp_done) {
        plan_message(p, QObject::tr("Done").toStdString());
        plan_run_cmd(p, (env.elevateTool + QStringLiteral(" ") + snapshotLib + QStringLiteral(" copy_log")).toStdString(), true);
        plan_abort(p, std::string("cleanUp exit: ") + reason);
        return;
    }

    plan_message(p, QObject::tr("Interrupted or failed to complete").toStdString());
    plan_run_cmd(p, (env.elevateTool + QStringLiteral(" ") + snapshotLib + QStringLiteral(" copy_log")).toStdString(), true);
    plan_abort(p, std::string("cleanUp exit: ") + reason);
}

} // namespace

WorkCppPlan WorkSetupEnvQtPlanOracle::planSetupEnv(const SettingsFields &settings, const Env &env)
{
    WorkCppPlan p;

    const QString bindRootPathForInstalledToLive = env.bindRootOverlayActive
                                                       ? QStringLiteral("/.bind-root")
                                                       : (env.setupBindRootOverlayOk
                                                              ? (QStringLiteral("/run/") + env.applicationName
                                                                 + QStringLiteral("/bind-root-overlay/root"))
                                                              : QStringLiteral("/.bind-root"));

    if (!env.workDirContainsS4Snapshot) {
        plan_abort(p, "Work dir does not look safe");
        return p;
    }

    plan_run_cmd(p, "mountpoint /boot", false);

    QString bind_boot;
    QString bind_boot_too;
    if (env.bootIsMountpoint) {
        bind_boot = QStringLiteral("bind=/boot ");
        bind_boot_too = QStringLiteral(",/boot");
    }

    if (settings.forceInstaller && env.needInstallCalamares) {
        plan_run_cmd(p, "INSTALL_PACKAGE calamares-settings-debian", false);
    }

    // writeSnapshotInfo()
    {
        const QString snapshotLib = QStringLiteral("/usr/lib/") + env.applicationName + QStringLiteral("/snapshot-lib");
        plan_run_cmd(p, (env.elevateTool + QStringLiteral(" ") + snapshotLib + QStringLiteral(" datetime_log")).toStdString(), true);
    }

    // writeVersionFile()
    {
        const QString filePath = env.mxVersionFileExistsInUsrLocal
                                     ? QStringLiteral("/usr/local/share/live-files/files/etc/mx-version")
                                     : QStringLiteral("/usr/share/live-files/files/etc/mx-version");
        const QString text = settings.fullDistroName + QStringLiteral(" ") + settings.codename + QStringLiteral(" ")
                             + settings.releaseDate + QStringLiteral("\n");
        plan_run_cmd(p, (QStringLiteral("WRITE_TEXT_FILE_UTF8_TRUNCATE ") + filePath + QStringLiteral(" ") + text).toStdString(), true);
    }

    // writeLsbRelease()
    {
        const QString filePath = env.lsbReleaseExistsInUsrLocal
                                     ? QStringLiteral("/usr/local/share/live-files/files/etc/lsb-release")
                                     : QStringLiteral("/usr/share/live-files/files/etc/lsb-release");
        const std::string content = WorkCppUtils::buildLsbReleaseContent(
            settings.projectName.toStdString(),
            settings.distroVersion.toStdString(),
            settings.codename.toStdString());
        plan_run_cmd(p,
                     (QStringLiteral("WRITE_TEXT_FILE_UTF8_TRUNCATE ") + filePath + QStringLiteral(" ")
                      + QString::fromStdString(content))
                         .toStdString(),
                     true);
    }

    {
        const QString overlayBase = QStringLiteral("/run/") + env.applicationName + QStringLiteral("/bind-root-overlay");
        const QString lowerDir = overlayBase + QStringLiteral("/lower");
        const QString upperDir = overlayBase + QStringLiteral("/upper");
        const QString workDir = overlayBase + QStringLiteral("/work");
        const QString bindRoot = overlayBase + QStringLiteral("/root");

        plan_proc_root(p,
                       "mkdir",
                       {"-p", overlayBase.toStdString(), lowerDir.toStdString(), upperDir.toStdString(), workDir.toStdString(),
                        bindRoot.toStdString()},
                       true);

        plan_proc_root(p, "mountpoint", {"-q", bindRoot.toStdString()}, true);

        if (env.setupBindRootOverlay_bindRootIsMountpoint) {
            plan_proc_root(p, "umount", {"--recursive", bindRoot.toStdString()}, true);
        }

        plan_proc_root(p, "mountpoint", {"-q", lowerDir.toStdString()}, true);
        if (env.setupBindRootOverlay_lowerIsMountpoint) {
            plan_proc_root(p, "umount", {"--recursive", lowerDir.toStdString()}, true);
        }

        plan_proc_root(p, "mount", {"--bind", "/", lowerDir.toStdString()}, true);
        if (!env.setupBindRootOverlay_bindMountOk) {
            plan_message_box(p,
                             BoxType::critical,
                             QObject::tr("Error").toStdString(),
                             QObject::tr("Could not prepare a safe bind-root overlay. Snapshot cannot continue.").toStdString());
            plan_abort(p, "setupBindRootOverlay failed: bind mount");
            plan_clean_up(p, env, "setupBindRootOverlay failed");
            return p;
        }

        const QString overlayOptions = QStringLiteral("lowerdir=") + lowerDir + QStringLiteral(",upperdir=") + upperDir
                                       + QStringLiteral(",workdir=") + workDir;
        plan_proc_root(p,
                       "mount",
                       {"-t", "overlay", "overlay", "-o", overlayOptions.toStdString(), bindRoot.toStdString()},
                       true);
        if (!env.setupBindRootOverlay_overlayMountOk) {
            plan_proc_root(p, "umount", {"--recursive", lowerDir.toStdString()}, true);
            plan_message_box(p,
                             BoxType::critical,
                             QObject::tr("Error").toStdString(),
                             QObject::tr("Could not prepare a safe bind-root overlay. Snapshot cannot continue.").toStdString());
            plan_abort(p, "setupBindRootOverlay failed: overlay mount");
            plan_clean_up(p, env, "setupBindRootOverlay failed");
            return p;
        }

        if (!env.setupBindRootOverlayOk) {
            plan_message_box(p,
                             BoxType::critical,
                             QObject::tr("Error").toStdString(),
                             QObject::tr("Could not prepare a safe bind-root overlay. Snapshot cannot continue.").toStdString());
            plan_abort(p, "setupBindRootOverlay failed");
            plan_clean_up(p, env, "setupBindRootOverlay failed");
            return p;
        }
    }

    plan_file_remove(p, "/var/lib/mxdebian/.mxsnapshot_accounts_reset.stp");

    if (settings.resetAccounts) {
        plan_proc_root(p, "mkdir", {"-p", "/var/lib/mxdebian/"}, true);
        plan_proc_root(p, "touch", {"-p", "/var/lib/mxdebian/.mxsnapshot_accounts_reset.stp"}, true);

        QStringList args {QStringLiteral("-F"),
                          QStringLiteral("-b"),
                          bindRootPathForInstalledToLive,
                          QStringLiteral("start")};
        if (!bind_boot.isEmpty()) {
            args << QStringLiteral("bind=/boot");
        }
        args << QStringLiteral("empty=/home") << QStringLiteral("general") << QStringLiteral("version-file");
        args << QStringLiteral("grubdefault") << QStringLiteral("resumedisable") << QStringLiteral("tdmnoautologin")
             << QStringLiteral("sddmnoautologin");

        std::vector<std::string> a;
        a.reserve(static_cast<size_t>(args.size()));
        for (const QString &x : args) {
            a.push_back(x.toStdString());
        }
        plan_proc_root(p, "installed-to-live", a, false);
        plan_run_cmd(p,
                     "CHECK_RESULT installed-to-live resetAccounts else ERROR: Could not prepare the snapshot bind-root environment.",
                     true);
    } else {
        QStringList args {QStringLiteral("-F"),
                          QStringLiteral("-b"),
                          bindRootPathForInstalledToLive,
                          QStringLiteral("start"),
                          QStringLiteral("bind=/home") + bind_boot_too,
                          QStringLiteral("live-files"),
                          QStringLiteral("version-file"),
                          QStringLiteral("adjtime")};
        args << QStringLiteral("grubdefault") << QStringLiteral("resumedisable");

        std::vector<std::string> a;
        a.reserve(static_cast<size_t>(args.size()));
        for (const QString &x : args) {
            a.push_back(x.toStdString());
        }
        plan_proc_root(p, "installed-to-live", a, false);
        plan_run_cmd(p,
                     "CHECK_RESULT installed-to-live normal else ERROR: Could not prepare the snapshot bind-root environment.",
                     true);
    }

    if (!env.bindRootOverlayActive && !env.setupBindRootOverlayOk) {
        plan_proc_root(p, "installed-to-live", {"-b", "/.bind-root", "read-only"}, true);
    }

    return p;
}
