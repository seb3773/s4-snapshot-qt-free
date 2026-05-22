#include "work_cleanup_qt_plan_oracle.h"

#include <QCoreApplication>
#include <QObject>

#include "stdio_cpp.h"

namespace {

static void plan_message(WorkCppPlan &p, const std::string &s)
{
    WorkCppPlanStep st;
    st.payload = WorkCppPlanStep::Message{s};
    p.steps.push_back(std::move(st));
}

static void plan_chdir(WorkCppPlan &p, const std::string &path)
{
    WorkCppPlanStep st;
    st.payload = WorkCppPlanStep::Chdir{path};
    p.steps.push_back(std::move(st));
}

static void plan_run_cmd(WorkCppPlan &p, const std::string &cmd, bool quietYes)
{
    WorkCppPlanStep st;
    st.payload = WorkCppPlanStep::RunCommandLine{cmd, quietYes};
    p.steps.push_back(std::move(st));
}

static void plan_process_execute(WorkCppPlan &p, const std::string &program, const std::vector<std::string> &args, int timeoutMs)
{
    WorkCppPlanStep st;
    st.payload = WorkCppPlanStep::ProcessExecute{program, args, timeoutMs};
    p.steps.push_back(std::move(st));
}

static void plan_file_copy(WorkCppPlan &p, const std::string &source, const std::string &destination)
{
    WorkCppPlanStep st;
    st.payload = WorkCppPlanStep::FileCopy{source, destination};
    p.steps.push_back(std::move(st));
}

static void plan_file_remove(WorkCppPlan &p, const std::string &path)
{
    WorkCppPlanStep st;
    st.payload = WorkCppPlanStep::FileRemove{path};
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

} // namespace

WorkCppPlan WorkCleanupQtPlanOracle::planCleanup(const SettingsFields &settings, const Env &env)
{
    WorkCppPlan p;

    // This replicates the Qt Work::cleanUp() method exactly
    const QString snapshotLib = "/usr/lib/" + env.applicationName + "/snapshot-lib";
    const QString elevateTool = env.elevateTool;

    // Step 1: chown_conf
    plan_run_cmd(p, (elevateTool + " " + snapshotLib + " chown_conf").toStdString(), true);

    // Step 2: Handle early exit if not started
    if (!env.started) {
        plan_temp_dir_remove(p, "initrd_dir");
        if (env.bindRootOverlayBaseNonEmpty) {
            plan_run_cmd(p,
                         (elevateTool + " " + snapshotLib + " cleanup_overlay " + env.applicationName).toStdString(),
                         true);
        }
        plan_abort(p, "cleanUp exit: not started");
        return p;
    }

    // Step 3: Show "Cleaning..." message
    plan_message(p, QObject::tr("Cleaning...").toStdString());

    // Step 4: Restore cursor visibility
    plan_run_cmd(p, "STDIO_WRITE_STDOUT \\033[?25h", true);

    // Step 5: Flush stdout
    plan_run_cmd(p, "STDIO_FLUSH_STDOUT", true);

    // Step 6: Kill mksquashfs
    plan_run_cmd(p, (elevateTool + " " + snapshotLib + " kill_mksquashfs").toStdString(), true);

    // Step 7: Sync
    plan_process_execute(p, "sync", {}, -1);

    // Step 8: Change to root directory
    plan_chdir(p, "/");

    // Step 9: Run cleanup if cleanup.conf exists
    if (env.cleanupConfExists) {
        plan_run_cmd(p, (elevateTool + " " + snapshotLib + " cleanup").toStdString(), false);
    }

    // Step 10: Cleanup bind-root overlay
    if (env.bindRootOverlayBaseNonEmpty) {
        plan_run_cmd(p, (elevateTool + " " + snapshotLib + " cleanup_overlay " + env.applicationName).toStdString(), true);
    }

    // Step 11: Remove accounts reset marker
    plan_file_remove(p, "/var/lib/mxdebian/.mxsnapshot_accounts_reset.stp");

    // Step 12: Remove initrd temp directory
    plan_temp_dir_remove(p, "initrd_dir");

    // Step 13: Reset tmpdir
    {
        WorkCppPlanStep st;
        st.payload = WorkCppPlanStep::TempDirRemove{"tmpdir"};
        p.steps.push_back(std::move(st));
    }

    // Step 14: Handle completion
    if (env.done) {
        plan_message(p, QObject::tr("Done").toStdString());
        plan_run_cmd(p, (elevateTool + " " + snapshotLib + " copy_log").toStdString(), true);
        
        if (settings.shutdown) {
            plan_file_copy(p,
                           ("/tmp/" + env.applicationName + ".log").toStdString(),
                           (settings.snapshotDir + "/" + settings.snapshotName + ".log").toStdString());
            plan_process_execute(p, "sync", {}, -1);
            plan_process_execute(p, "shutdown", {"-h", "now"}, 0);
        }
        plan_abort(p, "cleanUp exit: success");
    } else {
        plan_message(p, QObject::tr("Interrupted or failed to complete").toStdString());
        plan_run_cmd(p, (elevateTool + " " + snapshotLib + " copy_log").toStdString(), true);
        plan_abort(p, "cleanUp exit: failure");
    }

    return p;
}

// Made with Bob
