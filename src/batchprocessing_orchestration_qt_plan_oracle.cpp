#include "batchprocessing_orchestration_qt_plan_oracle.h"

#include <QObject>

#include "file_cpp.h"
#include "work_copynewiso_qt_plan_oracle.h"
#include "work_createiso_qt_plan_oracle.h"
#include "work_setupenv_qt_plan_oracle.h"

namespace {

static void plan_debug(BatchprocessingCppPlan &p, const std::string &text)
{
    BatchprocessingCppPlanStep st;
    st.payload = BatchprocessingCppPlanStep::Debug{text};
    p.steps.push_back(std::move(st));
}

static void plan_critical(BatchprocessingCppPlan &p, const std::string &text)
{
    BatchprocessingCppPlanStep st;
    st.payload = BatchprocessingCppPlanStep::Critical{text};
    p.steps.push_back(std::move(st));
}

static void plan_abort(BatchprocessingCppPlan &p, const std::string &reason)
{
    BatchprocessingCppPlanStep st;
    st.payload = BatchprocessingCppPlanStep::Abort{reason};
    p.steps.push_back(std::move(st));
}

static void plan_work(BatchprocessingCppPlan &p, const std::string &stage, const WorkCppPlan &plan)
{
    BatchprocessingCppPlanStep st;
    st.payload = BatchprocessingCppPlanStep::CallWorkPlan{stage, plan};
    p.steps.push_back(std::move(st));
}

} // namespace

BatchprocessingCppPlan BatchprocessingOrchestrationQtPlanOracle::planOrchestration(const SettingsFields &settings,
                                                                                 const Env &env)
{
    BatchprocessingCppPlan p;

    if (!env.checkCompressionOk) {
        plan_critical(p,
                      (QObject::tr("Error").toStdString() + std::string(" ")
                       + QObject::tr("Current kernel doesn't support selected compression algorithm, "
                                    "please edit the configuration file and select a different algorithm.")
                             .toStdString()));
        plan_abort(p, "checkCompression failed");
        return p;
    }

    plan_debug(p, "Free space: <computed>");
    if (!settings.monthly && !settings.overrideSize) {
        plan_debug(p, "Unused space: <computed>");
    }

    if (!env.checkSnapshotDirOk || !env.checkTempDirOk) {
        plan_work(p, "cleanUp", WorkCppPlan{});
        plan_abort(p, "snapshot/temp dir check failed");
        return p;
    }

    // Work::setupEnv
    {
        WorkSetupEnvQtPlanOracle::SettingsFields ws;
        ws.workDir = settings.workDir;
        ws.forceInstaller = settings.forceInstaller;
        ws.resetAccounts = settings.resetAccounts;
        ws.projectName = settings.projectName;
        ws.distroVersion = settings.distroVersion;
        ws.codename = settings.codename;
        ws.fullDistroName = settings.fullDistroName;
        ws.releaseDate = settings.releaseDate;

        WorkSetupEnvQtPlanOracle::Env we;
        we.workDirContainsS4Snapshot = env.setupEnv_workDirContainsS4Snapshot;
        we.bootIsMountpoint = env.setupEnv_bootIsMountpoint;
        we.bindRootOverlayActive = env.setupEnv_bindRootOverlayActive;
        we.needInstallCalamares = env.setupEnv_needInstallCalamares;
        we.setupBindRootOverlayOk = env.setupEnv_setupBindRootOverlayOk;
        we.setupBindRootOverlay_bindRootIsMountpoint = env.setupEnv_setupBindRootOverlay_bindRootIsMountpoint;
        we.setupBindRootOverlay_lowerIsMountpoint = env.setupEnv_setupBindRootOverlay_lowerIsMountpoint;
        we.setupBindRootOverlay_bindMountOk = env.setupEnv_setupBindRootOverlay_bindMountOk;
        we.setupBindRootOverlay_overlayMountOk = env.setupEnv_setupBindRootOverlay_overlayMountOk;
        we.applicationName = env.setupEnv_applicationName;
        we.elevateTool = env.setupEnv_elevateTool;
        we.mxVersionFileExistsInUsrLocal = env.setupEnv_mxVersionFileExistsInUsrLocal;
        we.lsbReleaseExistsInUsrLocal = env.setupEnv_lsbReleaseExistsInUsrLocal;

        we.cleanUp_started = env.setupEnv_cleanUp_started;
        we.cleanUp_done = env.setupEnv_cleanUp_done;
        we.cleanUp_cleanupConfExists = env.setupEnv_cleanUp_cleanupConfExists;
        we.cleanUp_bindRootOverlayBaseNonEmpty = env.setupEnv_cleanUp_bindRootOverlayBaseNonEmpty;

        plan_work(p, "setupEnv", WorkSetupEnvQtPlanOracle::planSetupEnv(ws, we));
    }

    if (!settings.monthly && !settings.overrideSize) {
        plan_work(p, "checkEnoughSpace", WorkCppPlan{});
    }

    // Work::copyNewIso
    {
        WorkCopyNewIsoQtPlanOracle::SettingsFields cs;
        cs.workDir = settings.workDir;
        cs.kernel = settings.kernel;
        cs.projectName = settings.projectName;
        cs.distroVersion = settings.distroVersion;
        cs.fullDistroName = settings.fullDistroName;
        cs.releaseDate = settings.releaseDate;
        cs.codename = settings.codename;
        cs.bootOptions = settings.bootOptions;

        WorkCopyNewIsoQtPlanOracle::Env ce;
        ce.isoTemplateMultiExists = env.copyNewIso_isoTemplateMultiExists;
        ce.sysvinitInitExists = env.copyNewIso_sysvinitInitExists;
        ce.systemdSystemdExists = env.copyNewIso_systemdSystemdExists;
        ce.initrdTempDirValid = env.copyNewIso_initrdTempDirValid;
        ce.initrdTempDirPath = env.copyNewIso_initrdTempDirPath;
        ce.loggedInUserName = env.copyNewIso_loggedInUserName;
        ce.applicationName = env.copyNewIso_applicationName;

        plan_work(p, "copyNewIso", WorkCopyNewIsoQtPlanOracle::planCopyNewIso(cs, ce));
    }

    // Work::savePackageList
    {
        WorkCppPlan wp;
        const std::string base = FileCpp::completeBaseName(settings.snapshotName.toStdString());
        const std::string fullName = settings.workDir.toStdString() + "/iso-template/" + base + "/package_list";
        const std::string cmd = std::string(R"(dpkg -l | awk '/^ii /{printf "%-41s %s\n", $2, $3}' > ')") + fullName
            + "'";

        WorkCppPlanStep st;
        st.payload = WorkCppPlanStep::RunCommandLine{cmd, false};
        wp.steps.push_back(std::move(st));
        plan_work(p, "savePackageList", wp);
    }

    if (env.editBootMenu) {
        plan_debug(p,
                   QObject::tr("The program will pause the build and open the boot menu in your text editor.")
                       .toStdString());

        WorkCppPlan wp;
        const std::string cmd = env.editorCmd.toStdString() + " \"" + settings.workDir.toStdString()
            + "/iso-template/boot/grub/grub.cfg\" \"" + settings.workDir.toStdString()
            + "/iso-template/boot/syslinux/syslinux.cfg\" \"" + settings.workDir.toStdString()
            + "/iso-template/boot/isolinux/isolinux.cfg\"";
        WorkCppPlanStep st;
        st.payload = WorkCppPlanStep::RunCommandLine{cmd, false};
        wp.steps.push_back(std::move(st));
        plan_work(p, "editBootMenu", wp);
    }

    // Work::createIso
    {
        WorkCreateIsoQtPlanOracle::SettingsFields is;
        is.workDir = settings.workDir;
        is.snapshotDir = QString();
        is.compression = settings.compression;
        is.cores = settings.cores;
        is.throttle = settings.throttle;
        is.mksqOpt = settings.mksqOpt;
        is.makeIsohybrid = settings.makeIsohybrid;
        is.makeMd5sum = settings.makeMd5sum;
        is.makeSha512sum = settings.makeSha512sum;

        plan_work(p, "createIso", WorkCreateIsoQtPlanOracle::planCreateIso(is, settings.snapshotName, env.createIsoEnv));
    }

    return p;
}
