#include "work_copynewiso_qt_plan_oracle.h"

#include <QObject>

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

static void plan_proc_root(WorkCppPlan &p,
                           const std::string &program,
                           const std::vector<std::string> &args,
                           bool quietYes)
{
    WorkCppPlanStep st;
    st.payload = WorkCppPlanStep::ProcAsRoot{program, args, quietYes};
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

static void plan_dir_remove_recursively(WorkCppPlan &p, const std::string &path)
{
    WorkCppPlanStep st;
    st.payload = WorkCppPlanStep::DirRemoveRecursively{path};
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

WorkCppPlan WorkCopyNewIsoQtPlanOracle::planCopyNewIso(const SettingsFields &settings, const Env &env)
{
    WorkCppPlan p;

    plan_message(p, QObject::tr("Copying the new-iso filesystem...").toStdString());
    plan_chdir(p, settings.workDir.toStdString());

    if (env.isoTemplateMultiExists && env.sysvinitInitExists && env.systemdSystemdExists) {
        plan_run_cmd(p, "tar xf /usr/lib/iso-template/iso-template-multi.tar.gz", false);
    } else {
        plan_run_cmd(p, "tar xf /usr/lib/iso-template/iso-template.tar.gz", false);
    }

    plan_run_cmd(p, "cp /usr/lib/iso-template/template-initrd.gz iso-template/antiX/initrd.gz", false);
    plan_run_cmd(p,
                 (QStringLiteral("cp /boot/vmlinuz-") + settings.kernel
                  + QStringLiteral(" iso-template/antiX/vmlinuz"))
                     .toStdString(),
                 false);

    const std::string replaceMenuCmd = std::string("REPLACE_MENU_STRINGS ")
                                       + settings.workDir.toStdString() + "|" + settings.projectName.toStdString() + "|"
                                       + settings.distroVersion.toStdString() + "|" + settings.fullDistroName.toStdString() + "|"
                                       + settings.releaseDate.toStdString() + "|" + settings.codename.toStdString() + "|"
                                       + settings.bootOptions.toStdString();
    plan_run_cmd(p, replaceMenuCmd, false);
    plan_run_cmd(p,
                 (QStringLiteral("MD5_CHECKSUM ") + settings.workDir + QStringLiteral("/iso-template/antiX vmlinuz"))
                     .toStdString(),
                 false);

    if (!env.initrdTempDirValid) {
        plan_abort(p, "Could not create temp directory");
        return p;
    }

    const QString path = env.initrdTempDirPath;
    plan_run_cmd(p,
                 (QStringLiteral("OPEN_INITRD ") + settings.workDir + QStringLiteral("/iso-template/antiX/initrd.gz ")
                  + path)
                     .toStdString(),
                 false);

    if (path.startsWith(QStringLiteral("/tmp/"))) {
        plan_dir_remove_recursively(p, (path + QStringLiteral("/lib/modules")).toStdString());
    }

    {
        const QString source = QStringLiteral("/etc/initrd-release");
        const QString dest = path + QStringLiteral("/etc/initrd-release");
        if (env.initrdReleaseExists && env.initrdReleaseIsFile) {
            if (!env.initrdReleaseDestExists) {
                plan_file_copy(p, source.toStdString(), dest.toStdString());
            } else {
                plan_file_remove(p, dest.toStdString());
                plan_file_copy(p, source.toStdString(), dest.toStdString());
            }
        }
    }

    {
        const QString source = QStringLiteral("/etc/initrd_release");
        const QString dest = path + QStringLiteral("/etc/initrd_release");
        if (env.initrd_releaseExists && env.initrd_releaseIsFile) {
            if (!env.initrd_releaseDestExists) {
                plan_file_copy(p, source.toStdString(), dest.toStdString());
            } else {
                plan_file_remove(p, dest.toStdString());
                plan_file_copy(p, source.toStdString(), dest.toStdString());
            }
        }
    }

    plan_run_cmd(p,
                 (QStringLiteral("/usr/share/") + env.applicationName
                  + QStringLiteral("/scripts/copy-initrd-modules -e -t=\"") + path
                  + QStringLiteral("\" -k=\"") + settings.kernel + QStringLiteral("\""))
                     .toStdString(),
                 false);
    plan_proc_root(p, "copy-initrd-programs", {"-e", "--to=" + path.toStdString()}, false);
    if (!env.loggedInUserName.isEmpty()) {
        plan_proc_root(p, "chown", {"-R", env.loggedInUserName.toStdString() + ":", path.toStdString()}, true);
    }

    plan_chdir(p, path.toStdString());
    plan_run_cmd(p,
                 (QStringLiteral("(find . |cpio -o -H newc --owner root:root |gzip -9) >\"") + settings.workDir
                  + QStringLiteral("/iso-template/antiX/initrd.gz\""))
                     .toStdString(),
                 false);
    plan_run_cmd(p,
                 (QStringLiteral("MD5_CHECKSUM ") + settings.workDir + QStringLiteral("/iso-template/antiX initrd.gz"))
                     .toStdString(),
                 false);

    plan_temp_dir_remove(p, "initrd_dir");
    return p;
}
