#include "batchprocessing_cpp_planner.h"

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

BatchprocessingCppPlan BatchprocessingCppPlanner::planOrchestration(const SettingsCpp &settings, const Env &env)
{
    BatchprocessingCppPlan p;

    // excludes prompt is handled elsewhere; Batchprocessing Qt calls it early.

    if (!env.checkCompressionOk) {
        plan_critical(p,
                      "Error Current kernel doesn't support selected compression algorithm, please edit the configuration file and select a different algorithm.");
        plan_abort(p, "checkCompression failed");
        return p;
    }

    // Free space / used space diagnostics are emitted at runtime by BatchprocessingCppRunner.

    if (!env.checkSnapshotDirOk || !env.checkTempDirOk) {
        plan_work(p, "cleanUp", WorkCppPlan{});
        plan_abort(p, "snapshot/temp dir check failed");
        return p;
    }

    // settings->otherExclusions() affects settings; planned implicitly.

    plan_work(p, "setupEnv", WorkCppPlanner::planSetupEnv(settings, env.setupEnvEnv));

    if (!settings.monthly && !settings.overrideSize) {
        plan_work(p, "checkEnoughSpace", WorkCppPlan{});
    }

    plan_work(p, "copyNewIso", WorkCppPlanner::planCopyNewIso(settings, env.copyNewIsoEnv));

    // savePackageList(snapshotName)
    plan_work(p, "savePackageList", WorkCppPlanner::planSavePackageList(settings, settings.snapshotName));

    if (env.editBootMenu) {
        plan_debug(p, "The program will pause the build and open the boot menu in your text editor.");
        plan_work(p, "editBootMenu", WorkCppPlanner::planEditBootMenu(settings, env.editorCmd));
    }

    plan_work(p, "createIso", WorkCppPlanner::planCreateIso(settings, settings.snapshotName, env.createIsoEnv));

    return p;
}
