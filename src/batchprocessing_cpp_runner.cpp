#include "batchprocessing_cpp_runner.h"

#include <cstdlib>
#include <iostream>
#include <utility>

#include "batchprocessing_cpp_planner.h"
#include "dir_cpp.h"
#include "file_cpp.h"
#include "filesystemutils_cpp.h"
#include "settings_editor_cpp.h"
#include "work_space_cpp.h"
#include "settings_exclusions_cpp.h"
#include "tempdir.h"
#include "work_cpp_utils.h"
#include "settings_tempdir_cpp.h"
#include "settings_snapshotdir_cpp.h"
#include "settings_debianver_cpp.h"

namespace {

static void cb_debug(const BatchprocessingCppRunner::Callbacks &cb, const std::string &text)
{
    if (cb.debug) {
        cb.debug(text);
    }

}

static void cb_critical(const BatchprocessingCppRunner::Callbacks &cb, const std::string &text);
static bool settings_check_temp_dir_like_qt(SettingsCpp &s,
                                           const BatchprocessingCppRunner::Callbacks &cb,
                                           TempDir *tmpOut);
static WorkCppPlanner::SetupEnvEnv compute_setupenv_env_like_qt(const SettingsCpp &s, const std::string &applicationName);

static BatchprocessingCppRunner::Result run_plan_with_runtime_space_check(const BatchprocessingCppPlan &plan,
                                                                         SettingsCpp &settings,
                                                                         const std::string &applicationName,
                                                                         const BatchprocessingCppRunner::Callbacks &cb,
                                                                         const BatchprocessingCppRunner::Dependencies &deps,
                                                                         TempDir *tmp)
{
    BatchprocessingCppRunner::Result out;

    for (const BatchprocessingCppPlanStep &st : plan.steps) {
        if (std::holds_alternative<BatchprocessingCppPlanStep::Debug>(st.payload)) {
            cb_debug(cb, std::get<BatchprocessingCppPlanStep::Debug>(st.payload).text);
            continue;
        }
        if (std::holds_alternative<BatchprocessingCppPlanStep::Critical>(st.payload)) {
            cb_critical(cb, std::get<BatchprocessingCppPlanStep::Critical>(st.payload).text);
            continue;
        }
        if (std::holds_alternative<BatchprocessingCppPlanStep::Abort>(st.payload)) {
            out.aborted = true;
            out.abortReason = std::get<BatchprocessingCppPlanStep::Abort>(st.payload).reason;
            return out;
        }
        if (std::holds_alternative<BatchprocessingCppPlanStep::CallWorkPlan>(st.payload)) {
            const auto &wp = std::get<BatchprocessingCppPlanStep::CallWorkPlan>(st.payload);

            if (wp.stage == "checkEnoughSpace") {
                // CRITICAL FIX: Initialize settings.freeSpace before checking space
                // This was missing in the C++ backend port, causing 0.00GiB to be reported
                // In the original Qt code, getFreeSpaceStrings() is called before checkEnoughSpace()
                // which initializes freeSpace. We need to do the same here.
                std::string path = settings.snapshotDir;
                // Remove "/snapshot" suffix if present (matching Qt logic)
                if (path.size() >= 9 && path.substr(path.size() - 9) == "/snapshot") {
                    path = path.substr(0, path.size() - 9);
                }
                settings.freeSpace = FileSystemUtilsCpp::getFreeSpaceKiB(path);
                cb_debug(cb, std::string("DEBUG: Initialized freeSpace = ") + std::to_string(settings.freeSpace) + " KiB");
                
                WorkSpaceCpp::Callbacks scb;
                scb.message = cb.debug;
                scb.warning = cb.critical;

                const WorkSpaceCpp::CheckEnoughSpaceResult sr
                    = WorkSpaceCpp::checkEnoughSpaceLikeQt(settings, applicationName, scb);

                cb_debug(cb, std::string("DEBUG: checkEnoughSpace result: ok=") + (sr.ok ? "true" : "false"));
                if (!sr.ok) {
                    if (!sr.messageBoxTitle.empty() || !sr.messageBoxText.empty()) {
                        cb_critical(cb, sr.messageBoxTitle + std::string(" ") + sr.messageBoxText);
                    }
                    out.aborted = true;
                    out.abortReason = "checkEnoughSpace failed";
                    return out;
                }
                cb_debug(cb, "DEBUG: checkEnoughSpace passed, continuing...");

                if (sr.shouldMoveWorkDir) {
                    settings.tempDirParent = sr.moveWorkDirTo;
                    if (!tmp) {
                        out.aborted = true;
                        out.abortReason = "checkEnoughSpace: missing TempDir handle for moveWorkDir";
                        return out;
                    }
                    if (!settings_check_temp_dir_like_qt(settings, cb, tmp)) {
                        out.aborted = true;
                        out.abortReason = "checkEnoughSpace: checkTempDir failed after moveWorkDir";
                        return out;
                    }

                    const WorkCppPlanner::SetupEnvEnv setupEnvEnv = compute_setupenv_env_like_qt(settings, applicationName);
                    const WorkCppPlan p = WorkCppPlanner::planSetupEnv(settings, setupEnvEnv);

                    if (!deps.runWork) {
                        out.aborted = true;
                        out.abortReason = "BatchprocessingCppRunner missing dependency: runWork";
                        return out;
                    }
                    WorkCppExecutor::Callbacks wcb;
                    wcb.message = cb.debug;
                    wcb.messageBox = [cb](BoxType /*type*/, const std::string &title, const std::string &text) {
                        cb_debug(cb, title + std::string(" ") + text);
                    };

                    const WorkCppExecutor::Result wr = deps.runWork(p, wcb);
                    if (wr.aborted) {
                        out.aborted = true;
                        out.abortReason = wr.abortReason;
                        return out;
                    }
                }

                cb_debug(cb, std::string("DEBUG: Finished work stage: ") + wp.stage);
                continue;
            }

            cb_debug(cb, std::string("DEBUG: About to run work stage: ") + wp.stage);
            std::cerr << "DEBUG: About to call deps.runWork() for stage: " << wp.stage << std::endl;
            
            if (!deps.runWork) {
                std::cerr << "ERROR: deps.runWork is NULL!" << std::endl;
                out.aborted = true;
                out.abortReason = "BatchprocessingCppRunner missing dependency: runWork";
                return out;
            }

            WorkCppExecutor::Callbacks wcb;
            wcb.message = cb.debug;
            wcb.messageBox = [cb](BoxType /*type*/, const std::string &title, const std::string &text) {
                cb_debug(cb, title + std::string(" ") + text);
            };

            std::cerr << "DEBUG: Calling deps.runWork() now..." << std::endl;
            const WorkCppExecutor::Result wr = deps.runWork(wp.plan, wcb);
            std::cerr << "DEBUG: deps.runWork() returned: aborted=" << wr.aborted << std::endl;
            
            if (wr.aborted) {
                out.aborted = true;
                out.abortReason = wr.abortReason;
                return out;
            }
            
            std::cerr << "DEBUG: Finished work stage: " << wp.stage << std::endl;
            continue;
        }

        out.aborted = true;
        out.abortReason = "BatchprocessingCppRunner encountered unknown plan step";
        return out;
    }

    return out;
}

static void cb_critical(const BatchprocessingCppRunner::Callbacks &cb, const std::string &text)
{
    if (cb.critical) {
        cb.critical(text);
    }
}

static std::string to_upper_ascii(std::string s)
{
    for (char &ch : s) {
        if (ch >= 'a' && ch <= 'z') {
            ch = static_cast<char>(ch - 'a' + 'A');
        }
    }
    return s;
}

static bool settings_check_compression_like_qt(const SettingsCpp &s)
{
    if (s.compression == "gzip") {
        return true;
    }
    if (s.kernel.empty()) {
        return true;
    }
    const std::string kernelCfg = std::string("/boot/config-") + s.kernel;
    if (!FileCpp::exists(kernelCfg)) {
        // Qt: if kernel config file missing, don't check.
        return true;
    }
    const std::string key = std::string("CONFIG_SQUASHFS_") + to_upper_ascii(s.compression) + "=y";
    return CommandRunner::run(std::string("grep ^") + key + " " + kernelCfg);
}

static bool settings_check_snapshot_dir_like_qt(const SettingsCpp &s, const BatchprocessingCppRunner::Callbacks &cb)
{
    SettingsSnapshotDirCpp::Callbacks scb;
    scb.debug = [&](const std::string &t) { cb_debug(cb, t); };
    return SettingsSnapshotDirCpp::checkSnapshotDirLikeSettingsQt(s, scb);
}

static bool settings_check_temp_dir_like_qt(SettingsCpp &s,
                                           const BatchprocessingCppRunner::Callbacks &cb,
                                           TempDir *tmpOut)
{
    SettingsTempDirCpp::Callbacks tcb;
    tcb.debug = [&](const std::string &t) { cb_debug(cb, t); };
    tcb.critical = [&](const std::string &t) { cb_critical(cb, t); };
    return SettingsTempDirCpp::checkTempDirLikeSettingsQt(s, tcb, tmpOut);
}

static WorkCppPlanner::SetupEnvEnv compute_setupenv_env_like_qt(const SettingsCpp &s, const std::string &applicationName)
{
    WorkCppPlanner::SetupEnvEnv env;

    env.applicationName = applicationName;
    env.elevateTool = CommandRunner::elevationTool();

    env.workDirContainsS4Snapshot = (s.workDir.find("/s4-snapshot") != std::string::npos);

    // mountpoint /boot
    {
        const CommandRunner::Result mp = CommandRunner::proc("mountpoint", {"/boot"}, std::string(), CommandRunner::QuietMode::Yes);
        env.bootIsMountpoint = (mp.started && mp.normalExit && mp.exitCode == 0);
    }

    // calamares install
    env.needInstallCalamares = s.forceInstaller && !WorkCppUtils::checkInstalled("calamares-settings-debian");

    // mx-version / lsb-release fallback
    env.mxVersionFileExistsInUsrLocal = FileCpp::exists("/usr/local/share/live-files/files/etc/mx-version");
    env.lsbReleaseExistsInUsrLocal = FileCpp::exists("/usr/local/share/live-files/files/etc/lsb-release");

    // setupBindRootOverlay pre-probe (mountpoints)
    const std::string overlayBase = std::string("/run/") + applicationName + "/bind-root-overlay";
    const std::string lowerDir = overlayBase + "/lower";
    const std::string bindRoot = overlayBase + "/root";
    {
        const CommandRunner::Result mp = CommandRunner::procAsRoot(
            "mountpoint", {"-q", bindRoot}, std::string(), CommandRunner::QuietMode::Yes);
        env.setupBindRootOverlay_bindRootIsMountpoint = (mp.started && mp.normalExit && mp.exitCode == 0);
    }
    {
        const CommandRunner::Result mp = CommandRunner::procAsRoot(
            "mountpoint", {"-q", lowerDir}, std::string(), CommandRunner::QuietMode::Yes);
        env.setupBindRootOverlay_lowerIsMountpoint = (mp.started && mp.normalExit && mp.exitCode == 0);
    }

    // We do not execute the mounts at this stage; assume OK and let real system run them.
    // Failures are handled via plan abort paths when env flags are set false in tests.
    env.setupBindRootOverlay_bindMountOk = true;
    env.setupBindRootOverlay_overlayMountOk = true;
    env.setupBindRootOverlayOk = true;
    env.bindRootOverlayActive = false;

    // cleanUp planning defaults
    env.cleanUp_started = true;
    env.cleanUp_done = false;
    env.cleanUp_cleanupConfExists = false;
    env.cleanUp_bindRootOverlayBaseNonEmpty = false;

    return env;
}

static int get_debian_ver_num_like_settings_qt()
{
    SettingsDebianVerNumCpp::Callbacks cb;
    // Keep runner behavior stable: do not emit Qt-critical lines from here.
    cb.critical = nullptr;
    return SettingsDebianVerNumCpp::getDebianVerNumLikeSettingsQt(cb);
}

} // namespace

BatchprocessingCppRunner::Result BatchprocessingCppRunner::run(const BatchprocessingCppPlan &plan,
                                                             const SettingsCpp & /*settings*/,
                                                             const Callbacks &cb,
                                                             const Dependencies &deps)
{
    std::cerr << "=== BatchprocessingCppRunner::run() START ===" << std::endl;
    std::cerr << "DEBUG: Callbacks - debug: " << (cb.debug ? "SET" : "NULL")
              << ", critical: " << (cb.critical ? "SET" : "NULL") << std::endl;
    std::cerr << "DEBUG: Dependencies - runWork: " << (deps.runWork ? "SET" : "NULL") << std::endl;
    std::cerr << "DEBUG: Plan has " << plan.steps.size() << " steps" << std::endl;
    
    Result out;

    for (const BatchprocessingCppPlanStep &st : plan.steps) {
        if (std::holds_alternative<BatchprocessingCppPlanStep::Debug>(st.payload)) {
            cb_debug(cb, std::get<BatchprocessingCppPlanStep::Debug>(st.payload).text);
            continue;
        }
        if (std::holds_alternative<BatchprocessingCppPlanStep::Critical>(st.payload)) {
            cb_critical(cb, std::get<BatchprocessingCppPlanStep::Critical>(st.payload).text);
            continue;
        }
        if (std::holds_alternative<BatchprocessingCppPlanStep::Abort>(st.payload)) {
            out.aborted = true;
            out.abortReason = std::get<BatchprocessingCppPlanStep::Abort>(st.payload).reason;
            cb_debug(cb, std::string("DEBUG: Plan aborted with reason: ") + out.abortReason);
            return out;
        }
        if (std::holds_alternative<BatchprocessingCppPlanStep::CallWorkPlan>(st.payload)) {
            const auto &wp = std::get<BatchprocessingCppPlanStep::CallWorkPlan>(st.payload);
            cb_debug(cb, std::string("DEBUG: Executing work stage: ") + wp.stage);
            if (!deps.runWork) {
                out.aborted = true;
                out.abortReason = "BatchprocessingCppRunner missing dependency: runWork";
                return out;
            }

            WorkCppExecutor::Callbacks wcb;
            wcb.message = cb.debug;
            wcb.messageBox = [cb](BoxType /*type*/, const std::string &title, const std::string &text) {
                cb_debug(cb, title + std::string(" ") + text);
            };

            const WorkCppExecutor::Result wr = deps.runWork(wp.plan, wcb);
            if (wr.aborted) {
                out.aborted = true;
                out.abortReason = wr.abortReason;
                return out;
            }
            continue;
        }

        out.aborted = true;
        out.abortReason = "BatchprocessingCppRunner encountered unknown plan step";
        return out;
    }

    return out;
}

BatchprocessingCppRunner::Result BatchprocessingCppRunner::runFromSettings(SettingsCpp settings,
                                                                          const std::string &applicationName,
                                                                          const Callbacks &cb,
                                                                          const Dependencies &deps)
{
    Result out;
    out.settings = settings;

    BatchprocessingCppPlanner::Env env;

    env.checkCompressionOk = settings_check_compression_like_qt(out.settings);
    if (!env.checkCompressionOk) {
        const BatchprocessingCppPlan p = BatchprocessingCppPlanner::planOrchestration(out.settings, env);
        const Result r = run(p, out.settings, cb, deps);
        out.aborted = r.aborted;
        out.abortReason = r.abortReason;
        return out;
    }

    // Snapshot dir / temp dir checks have side-effects and must run before planning.
    env.checkSnapshotDirOk = settings_check_snapshot_dir_like_qt(out.settings, cb);

    TempDir tmp;
    env.checkTempDirOk = settings_check_temp_dir_like_qt(out.settings, cb, &tmp);

    if (!env.checkSnapshotDirOk || !env.checkTempDirOk) {
        const BatchprocessingCppPlan p = BatchprocessingCppPlanner::planOrchestration(out.settings, env);
        const Result r = run(p, out.settings, cb, deps);
        out.aborted = r.aborted;
        out.abortReason = r.abortReason;
        return out;
    }

    // otherExclusions mutates sessionExcludes.
    SettingsExclusionsCpp::otherExclusionsLikeSettingsQt(out.settings);

    env.editBootMenu = out.settings.editBootMenu;
    env.editorCmd = SettingsEditorCpp::getEditorLikeSettingsQt(out.settings.guiEditor);

    env.setupEnvEnv = compute_setupenv_env_like_qt(out.settings, applicationName);

    // copyNewIso/createIso env require runtime probing; keep conservative defaults.
    env.copyNewIsoEnv.applicationName = applicationName;
    env.copyNewIsoEnv.loggedInUserName = CommandRunner::loggedInUserName();
    env.copyNewIsoEnv.isoTemplateMultiExists = FileCpp::exists("/usr/lib/iso-template/iso-template-multi.tar.gz");
    env.copyNewIsoEnv.sysvinitInitExists = FileCpp::exists("/usr/lib/sysvinit/init");
    env.copyNewIsoEnv.systemdSystemdExists = FileCpp::exists("/usr/lib/systemd/systemd");
    {
        TempDir initrdTmp("/tmp/s4-snapshot-initrd-XXXXXXXX");
        env.copyNewIsoEnv.initrdTempDirValid = initrdTmp.isValid();
        env.copyNewIsoEnv.initrdTempDirPath = initrdTmp.path();
        // temp dir will be auto-removed at end of scope; plan uses it for messages only.
        initrdTmp.setAutoRemove(true);
    }
    env.copyNewIsoEnv.initrdReleaseExists = FileCpp::exists("/etc/initrd-release");
    env.copyNewIsoEnv.initrdReleaseIsFile = FileCpp::isFile("/etc/initrd-release");
    env.copyNewIsoEnv.initrdReleaseDestExists = false;
    env.copyNewIsoEnv.initrd_releaseExists = FileCpp::exists("/etc/initrd_release");
    env.copyNewIsoEnv.initrd_releaseIsFile = FileCpp::isFile("/etc/initrd_release");
    env.copyNewIsoEnv.initrd_releaseDestExists = false;

    env.createIsoEnv.applicationName = applicationName;
    env.createIsoEnv.useUnbuffer = WorkCppUtils::checkInstalled("expect");
    env.createIsoEnv.umaskOut = CommandRunner::getOut("umask", CommandRunner::QuietMode::Yes);
    env.createIsoEnv.debianVerNum = get_debian_ver_num_like_settings_qt();
    // Set bindRootPath from overlay setup (matches what setupBindRootOverlay creates)
    env.createIsoEnv.bindRootPath = std::string("/run/") + applicationName + "/bind-root-overlay/root";

    const BatchprocessingCppPlan plan = BatchprocessingCppPlanner::planOrchestration(out.settings, env);
    const Result r = run_plan_with_runtime_space_check(plan, out.settings, applicationName, cb, deps, &tmp);
    out.aborted = r.aborted;
    out.abortReason = r.abortReason;

    // Keep tempdir alive for the duration of the run (scope).
    (void)tmp;
    return out;
}
