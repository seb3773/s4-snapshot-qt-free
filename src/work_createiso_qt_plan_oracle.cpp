#include "work_createiso_qt_plan_oracle.h"

#include <QCoreApplication>

#include "work_cpp_utils.h"

namespace {

static QStringList splitShellWords(const QString &text)
{
    const QString t = text.trimmed();
    if (t.isEmpty()) {
        return {};
    }

    QStringList out;
    out.reserve(t.size() / 2);
    QString cur;
    bool inSingle = false;
    bool inDouble = false;
    bool escape = false;

    for (const QChar ch : t) {
        if (escape) {
            cur.append(ch);
            escape = false;
            continue;
        }
        if (ch == QLatin1Char('\\')) {
            escape = true;
            continue;
        }
        if (!inDouble && ch == QLatin1Char('\'')) {
            inSingle = !inSingle;
            continue;
        }
        if (!inSingle && ch == QLatin1Char('"')) {
            inDouble = !inDouble;
            continue;
        }
        if (!inSingle && !inDouble && ch.isSpace()) {
            if (!cur.isEmpty()) {
                out.push_back(cur);
                cur.clear();
            }
            continue;
        }
        cur.append(ch);
    }
    if (!cur.isEmpty()) {
        out.push_back(cur);
    }
    return out;
}

static QString squashfsSessionExcludeArg(QString path)
{
    if (!path.isEmpty() && path.front() == QLatin1Char('/')) {
        path.remove(0, 1);
    }
    return path;
}

static void plan_message(WorkCppPlan &p, const std::string &s)
{
    WorkCppPlanStep st;
    st.payload = WorkCppPlanStep::Message{s};
    p.steps.push_back(std::move(st));
}

static void plan_message_box(WorkCppPlan &p, BoxType t, const std::string &title, const std::string &text)
{
    WorkCppPlanStep st;
    st.payload = WorkCppPlanStep::MessageBox{t, title, text};
    p.steps.push_back(std::move(st));
}

static void plan_chdir(WorkCppPlan &p, const std::string &path)
{
    WorkCppPlanStep st;
    st.payload = WorkCppPlanStep::Chdir{path};
    p.steps.push_back(std::move(st));
}

static void plan_mkpath(WorkCppPlan &p, const std::string &path)
{
    WorkCppPlanStep st;
    st.payload = WorkCppPlanStep::Mkpath{path};
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

} // namespace

WorkCppPlan WorkCreateIsoQtPlanOracle::planCreateIso(const SettingsFields &settings, const QString &filename, const Env &env)
{
    WorkCppPlan p;

    plan_chdir(p, settings.workDir.toStdString());

    const QString bindRootPath = env.bindRootPath;

    QStringList squashfsArgs {bindRootPath,
                              settings.workDir + QStringLiteral("/iso-template/antiX/linuxfs"),
                              QStringLiteral("-comp"),
                              settings.compression,
                              QStringLiteral("-processors"),
                              QString::number(settings.cores)};

    if (env.debianVerNum >= 12) {
        squashfsArgs << QStringLiteral("-throttle") << QString::number(settings.throttle);
    }

    squashfsArgs += splitShellWords(settings.mksqOpt);
    squashfsArgs << QStringLiteral("-wildcards") << QStringLiteral("-ef") << settings.snapshotExcludesPath;

    const QStringList sessionExcludes = splitShellWords(settings.sessionExcludes);
    if (!sessionExcludes.isEmpty()) {
        squashfsArgs << QStringLiteral("-e");
        for (QString exclude : sessionExcludes) {
            squashfsArgs << squashfsSessionExcludeArg(exclude);
        }
    }

    const QString wrapperCommand = env.useUnbuffer ? QStringLiteral("unbuffer") : QStringLiteral("stdbuf");
    QStringList wrapperArgs;
    if (!env.useUnbuffer) {
        wrapperArgs << QStringLiteral("-o0");
    }
    wrapperArgs << QStringLiteral("mksquashfs");
    wrapperArgs += squashfsArgs;

    plan_message(p, QObject::tr("Squashing filesystem...").toStdString());
    {
        std::vector<std::string> argsStd;
        argsStd.reserve(static_cast<size_t>(wrapperArgs.size()));
        for (const QString &a : wrapperArgs) {
            argsStd.push_back(a.toStdString());
        }
        plan_proc_root(p, wrapperCommand.toStdString(), argsStd, false);
    }

    plan_run_cmd(p,
                 std::string("test -s ")
                     + WorkCppUtils::shellQuoteSingleQuoted(
                         (settings.workDir + QStringLiteral("/iso-template/antiX/linuxfs")).toStdString()),
                 false);
    plan_run_cmd(p,
                 "CHECK_RESULT else ERROR: Could not create linuxfs file, please check /var/log/s4-snapshot.log",
                 false);
    plan_run_cmd(p,
                 std::string("WRITE_LINUXFS_INFO_FROM_MKSQUASHFS_OUTPUT ")
                     + (settings.workDir + QStringLiteral("/iso-template/antiX/linuxfs.info")).toStdString(),
                 true);

    plan_mkpath(p, std::string("iso-2/antiX"));
    plan_run_cmd(p, std::string("mv iso-template/antiX/linuxfs* iso-2/antiX"), false);
    plan_run_cmd(p,
                 "CHECK_RESULT else ERROR: Could not move linuxfs into iso-2.",
                 false);

    // md5 linuxfs
    plan_message(p, QObject::tr("Calculating checksum...").toStdString());
    plan_run_cmd(p, std::string("sync"), false);
    plan_chdir(p, (settings.workDir + QStringLiteral("/iso-2/antiX")).toStdString());
    plan_run_cmd(p,
                 std::string("md5sum ") + WorkCppUtils::shellQuoteSingleQuoted("linuxfs") + " > "
                     + WorkCppUtils::shellQuoteSingleQuoted(
                         (settings.workDir + QStringLiteral("/iso-2/antiX/linuxfs.md5")).toStdString()),
                 false);
    plan_chdir(p, settings.workDir.toStdString());

    plan_proc_root(p, "cleanup", {}, true);

    plan_chdir(p, (settings.workDir + QStringLiteral("/iso-template")).toStdString());

    const std::string isoFinalPath = (settings.snapshotDir + QStringLiteral("/") + filename).toStdString();
    const std::string isoTempPath = isoFinalPath + ".tmp.$$";
    const std::string iso2Dir = (settings.workDir + QStringLiteral("/iso-2")).toStdString();

    plan_run_cmd(p, std::string("rm -f ") + WorkCppUtils::shellQuoteSingleQuoted(isoTempPath), true);

    std::string xorrisoCmd =
        "xorriso -as mkisofs -l -V DEBIANLIVE -R -J -pad -iso-level 3 -no-emul-boot -boot-load-size 4 -boot-info-table "
        "-b boot/isolinux/isolinux.bin -eltorito-alt-boot -e boot/grub/efi.img -no-emul-boot -c "
        "boot/isolinux/isolinux.cat -o "
        + WorkCppUtils::shellQuoteSingleQuoted(isoTempPath) + " . "
        + WorkCppUtils::shellQuoteSingleQuoted(iso2Dir);

    if (env.umaskOut.trimmed() != QStringLiteral("0022")) {
        xorrisoCmd = "umask 022; " + xorrisoCmd;
    }

    plan_message(p, QObject::tr("Creating CD/DVD image file...").toStdString());
    plan_run_cmd(p, xorrisoCmd, false);
    plan_run_cmd(p, "CHECK_RESULT else ERROR: Could not create ISO file, please check whether you have enough space on the destination partition.", false);
    plan_run_cmd(p,
                 std::string("mv -f ") + WorkCppUtils::shellQuoteSingleQuoted(isoTempPath) + " "
                     + WorkCppUtils::shellQuoteSingleQuoted(isoFinalPath),
                 false);
    plan_run_cmd(p, "CHECK_RESULT else ERROR: Could not finalize ISO file.", false);
    plan_run_cmd(p, std::string("FSYNC_FILE ") + isoFinalPath, false);

    if (settings.makeIsohybrid) {
        plan_message(p, QObject::tr("Making hybrid iso").toStdString());
        plan_run_cmd(p,
                     std::string("isohybrid --uefi ")
                         + WorkCppUtils::shellQuoteSingleQuoted(isoFinalPath),
                     false);
        plan_run_cmd(p,
                     "CHECK_RESULT else ERROR: Could not make the ISO bootable as a hybrid image.",
                     false);
    }

    if (settings.makeMd5sum) {
        plan_message(p, QObject::tr("Calculating checksum...").toStdString());
        plan_run_cmd(p, std::string("sync"), false);
        plan_chdir(p, settings.snapshotDir.toStdString());
        plan_run_cmd(p,
                     std::string("md5sum ") + WorkCppUtils::shellQuoteSingleQuoted(filename.toStdString()) + " > "
                         + WorkCppUtils::shellQuoteSingleQuoted(
                             (settings.snapshotDir + QStringLiteral("/") + filename + QStringLiteral(".md5")).toStdString()),
                     false);
        plan_chdir(p, settings.workDir.toStdString());
    }

    if (settings.makeSha512sum) {
        plan_message(p, QObject::tr("Calculating checksum...").toStdString());
        plan_run_cmd(p, std::string("sync"), false);
        plan_chdir(p, settings.snapshotDir.toStdString());
        plan_run_cmd(p,
                     std::string("sha512sum ") + WorkCppUtils::shellQuoteSingleQuoted(filename.toStdString()) + " > "
                         + WorkCppUtils::shellQuoteSingleQuoted(
                             (settings.snapshotDir + QStringLiteral("/") + filename + QStringLiteral(".sha512"))
                                 .toStdString()),
                     false);
        plan_chdir(p, settings.workDir.toStdString());
    }

    plan_message(p, QObject::tr("Done").toStdString());

    plan_message_box(p,
                     BoxType::information,
                     QObject::tr("Success").toStdString(),
                     QObject::tr("Debian Snapshot completed successfully!").toStdString()
                         + "\n"
                         + QObject::tr("Snapshot took %1 to finish.").arg(QStringLiteral("<ELAPSED>")).toStdString()
                         + "\n\n"
                         + QObject::tr("Thanks for using Debian Snapshot, run Live USB Maker next!").toStdString());

    return p;
}
