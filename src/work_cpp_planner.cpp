#include "work_cpp_planner.h"

#include <cstdlib>

#include "dir_cpp.h"
#include "file_cpp.h"
#include "work_cpp_utils.h"

namespace {

static std::string trim_ascii(const std::string &s)
{
    std::size_t b = 0;
    while (b < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[b]);
        if (!(c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v')) {
            break;
        }
        ++b;
    }
    std::size_t e = s.size();
    while (e > b) {
        const unsigned char c = static_cast<unsigned char>(s[e - 1]);
        if (!(c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v')) {
            break;
        }
        --e;
    }
    return s.substr(b, e - b);
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

static void plan_clean_up(WorkCppPlan &p,
                          const SettingsCpp &settings,
                          const WorkCppPlanner::SetupEnvEnv &env,
                          const std::string &reason)
{
    plan_proc_root(p, "chown_conf", {}, true);

    if (!env.cleanUp_started) {
        plan_temp_dir_remove(p, "initrd_dir");
        if (env.cleanUp_bindRootOverlayBaseNonEmpty) {
            plan_proc_root(p, "cleanup_overlay", {env.applicationName}, true);
        }
        plan_abort(p, std::string("cleanUp exit: ") + reason);
        return;
    }

    plan_message(p, "Cleaning...");
    plan_proc_root(p, "kill_mksquashfs", {}, true);
    plan_process_execute(p, "sync", {}, -1);
    plan_chdir(p, "/");

    if (env.cleanUp_cleanupConfExists) {
        plan_proc_root(p, "cleanup", {}, false);
    }

    if (env.cleanUp_bindRootOverlayBaseNonEmpty) {
        plan_proc_root(p, "cleanup_overlay", {env.applicationName}, true);
    }
    plan_file_remove(p, "/var/lib/mxdebian/.mxsnapshot_accounts_reset.stp");
    plan_temp_dir_remove(p, "initrd_dir");

    if (env.cleanUp_done) {
        plan_message(p, "Done");
        plan_proc_root(p, "copy_log", {}, true);
        if (settings.shutdown) {
            plan_file_copy(p,
                           std::string("/tmp/") + env.applicationName + ".log",
                           settings.snapshotDir + "/" + settings.snapshotName + ".log");
            plan_process_execute(p, "sync", {}, -1);
            plan_process_execute(p, "shutdown", {"-h", "now"}, 0);
        }
        plan_abort(p, std::string("cleanUp exit: ") + reason);
        return;
    }

    plan_message(p, "Interrupted or failed to complete");
    plan_proc_root(p, "copy_log", {}, true);
    plan_abort(p, std::string("cleanUp exit: ") + reason);
}

} // namespace

std::vector<std::string> WorkCppPlanner::splitShellWordsLikeQt(const std::string &text)
{
    const std::string t = trim_ascii(text);
    if (t.empty()) {
        return {};
    }

    std::vector<std::string> out;
    out.reserve(t.size() / 2);
    std::string cur;
    bool inSingle = false;
    bool inDouble = false;
    bool escape = false;

    for (char ch : t) {
        if (escape) {
            cur.push_back(ch);
            escape = false;
            continue;
        }
        if (ch == '\\') {
            escape = true;
            continue;
        }
        if (!inDouble && ch == '\'') {
            inSingle = !inSingle;
            continue;
        }
        if (!inSingle && ch == '"') {
            inDouble = !inDouble;
            continue;
        }
        const bool isSpace = (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v');
        if (!inSingle && !inDouble && isSpace) {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
            continue;
        }
        cur.push_back(ch);
    }

    if (!cur.empty()) {
        out.push_back(cur);
    }

    return out;
}

WorkCppPlan WorkCppPlanner::planSavePackageList(const SettingsCpp &settings, const std::string &fileName)
{
    WorkCppPlan p;

    const std::string base = FileCpp::completeBaseName(fileName);
    const std::string fullName = settings.workDir + "/iso-template/" + base + "/package_list";
    const std::string cmd = std::string(R"(dpkg -l | awk '/^ii /{printf "%-41s %s\n", $2, $3}' > ')") + fullName
        + "'";

    WorkCppPlanStep st;
    st.payload = WorkCppPlanStep::RunCommandLine{cmd, false};
    p.steps.push_back(std::move(st));
    return p;
}

WorkCppPlan WorkCppPlanner::planEditBootMenu(const SettingsCpp &settings, const std::string &editorCmd)
{
    WorkCppPlan p;

    const std::string cmd = editorCmd + " \"" + settings.workDir + "/iso-template/boot/grub/grub.cfg\" \""
        + settings.workDir + "/iso-template/boot/syslinux/syslinux.cfg\" \"" + settings.workDir
        + "/iso-template/boot/isolinux/isolinux.cfg\"";

    WorkCppPlanStep st;
    st.payload = WorkCppPlanStep::RunCommandLine{cmd, false};
    p.steps.push_back(std::move(st));
    return p;
}

WorkCppPlan WorkCppPlanner::planCreateIso(const SettingsCpp &settings,
                                         const std::string &filename,
                                         const CreateIsoEnv &env)
{
    WorkCppPlan p;
    
    // Set working directory at the start of createIso stage
    // This is necessary because copyNewIso ends with cd to temp initrd dir
    plan_chdir(p, settings.workDir);

    // --- mksquashfs stage
    // Use bindRootPath from env (set by overlay setup)
    std::vector<std::string> squashfsArgs;
    squashfsArgs.push_back(env.bindRootPath);
    squashfsArgs.push_back(settings.workDir + "/iso-template/antiX/linuxfs");
    squashfsArgs.push_back("-comp");
    squashfsArgs.push_back(settings.compression);
    squashfsArgs.push_back("-processors");
    squashfsArgs.push_back(std::to_string(settings.cores));

    if (env.debianVerNum >= 12) {
        squashfsArgs.push_back("-throttle");
        squashfsArgs.push_back(std::to_string(settings.throttle));
    }

    for (const std::string &a : splitShellWordsLikeQt(settings.mksqOpt)) {
        squashfsArgs.push_back(a);
    }

    squashfsArgs.push_back("-wildcards");
    squashfsArgs.push_back("-ef");
    squashfsArgs.push_back(settings.snapshotExcludesPath);

    const std::vector<std::string> sessionExcludes = splitShellWordsLikeQt(settings.sessionExcludes);
    if (!sessionExcludes.empty()) {
        squashfsArgs.push_back("-e");
        for (const std::string &a : sessionExcludes) {
            squashfsArgs.push_back(a);
        }
    }

    const std::string wrapperCommand = env.useUnbuffer ? "unbuffer" : "stdbuf";
    std::vector<std::string> wrapperArgs;
    if (!env.useUnbuffer) {
        wrapperArgs.push_back("-o0");
    }
    wrapperArgs.push_back("mksquashfs");
    wrapperArgs.insert(wrapperArgs.end(), squashfsArgs.begin(), squashfsArgs.end());

    plan_message(p, "Squashing filesystem...");
    plan_proc_root(p, wrapperCommand, wrapperArgs, false);

    // Side-effect derived from mksquashfs output (unknown at plan time)
    plan_run_cmd(p,
                 std::string("WRITE_LINUXFS_INFO_FROM_MKSQUASHFS_OUTPUT ")
                     + (settings.workDir + "/iso-template/antiX/linuxfs.info"),
                 true);

    // --- move linuxfs to iso-2
    plan_mkpath(p, "iso-2/antiX");
    plan_run_cmd(p, "mv iso-template/antiX/linuxfs* iso-2/antiX", false);

    // --- checksum linuxfs
    plan_message(p, "Calculating checksum...");
    plan_run_cmd(p, "sync", false);
    plan_chdir(p, settings.workDir + "/iso-2/antiX");
    plan_run_cmd(p,
                 std::string("md5sum \"") + "linuxfs" + "\">\"" + (settings.workDir + "/iso-2/antiX")
                     + "/linuxfs.md5\"",
                 false);
    plan_chdir(p, settings.workDir);

    // --- cleanup
    plan_proc_root(p, "cleanup", {}, true);

    // --- xorriso stage
    plan_chdir(p, settings.workDir + "/iso-template");
    std::string xorrisoCmd =
        "xorriso -as mkisofs -l -V DEBIANLIVE -R -J -pad -iso-level 3 -no-emul-boot -boot-load-size 4 -boot-info-table "
        "-b boot/isolinux/isolinux.bin -eltorito-alt-boot -e boot/grub/efi.img -no-emul-boot -c "
        "boot/isolinux/isolinux.cat -o \"";
    xorrisoCmd += settings.snapshotDir + "/" + filename;
    xorrisoCmd += "\" . \"";
    xorrisoCmd += settings.workDir + "/iso-2\"";

    if (trim_ascii(env.umaskOut) != "0022") {
        xorrisoCmd = "umask 022; " + xorrisoCmd;
    }

    plan_message(p, "Creating CD/DVD image file...");
    plan_run_cmd(p, xorrisoCmd, false);
    
    // Check if xorriso succeeded - match Qt behavior (work.cpp:676-681)
    plan_run_cmd(p, "CHECK_RESULT else ERROR: Could not create ISO file, please check whether you have enough space on the destination partition.", false);

    if (settings.makeIsohybrid) {
        plan_message(p, "Making hybrid iso");
        plan_run_cmd(p,
                     std::string("isohybrid --uefi \"") + (settings.snapshotDir + "/" + filename) + "\"",
                     false);
    }

    if (settings.makeMd5sum) {
        plan_message(p, "Calculating checksum...");
        plan_run_cmd(p, "sync", false);
        plan_chdir(p, settings.snapshotDir);
        plan_run_cmd(p,
                     std::string("md5sum \"") + filename + "\">\"" + settings.snapshotDir + "/" + filename
                         + ".md5\"",
                     false);
        plan_chdir(p, settings.workDir);
    }

    if (settings.makeSha512sum) {
        plan_message(p, "Calculating checksum...");
        plan_run_cmd(p, "sync", false);
        plan_chdir(p, settings.snapshotDir);
        plan_run_cmd(p,
                     std::string("sha512sum \"") + filename + "\">\"" + settings.snapshotDir + "/" + filename
                         + ".sha512\"",
                     false);
        plan_chdir(p, settings.workDir);
    }

    plan_message(p, "Done");

    plan_message_box(p,
                     BoxType::information,
                     "Success",
                     std::string("Debian Snapshot completed successfully!\n")
                         + "Snapshot took <ELAPSED> to finish.\n\n"
                         + "Thanks for using Debian Snapshot, run Live USB Maker next!");

    return p;
}

WorkCppPlan WorkCppPlanner::planCopyNewIso(const SettingsCpp &settings, const CopyNewIsoEnv &env)
{
    WorkCppPlan p;

    plan_message(p, "Copying the new-iso filesystem...");
    plan_chdir(p, settings.workDir);

    plan_run_cmd(p,
                 std::string("EMBED_EXTRACT_ISO_TEMPLATE ") + settings.workDir + "/iso-template",
                 false);
    plan_run_cmd(p,
                 std::string("cp /boot/vmlinuz-") + settings.kernel + " iso-template/antiX/vmlinuz",
                 false);

    // Encode settings into REPLACE_MENU_STRINGS command
    // Format: REPLACE_MENU_STRINGS <workDir>|<projectName>|<distroVersion>|<fullDistroName>|<releaseDate>|<codename>|<bootOptions>
    const std::string replaceMenuCmd = std::string("REPLACE_MENU_STRINGS ")
        + settings.workDir + "|"
        + settings.projectName + "|"
        + settings.distroVersion + "|"
        + settings.fullDistroName + "|"
        + settings.releaseDate + "|"
        + settings.codename + "|"
        + settings.bootOptions;
    plan_run_cmd(p, replaceMenuCmd, false);
    plan_run_cmd(p,
                 std::string("MD5_CHECKSUM ") + settings.workDir + "/iso-template/antiX vmlinuz",
                 false);

    if (!env.initrdTempDirValid) {
        plan_abort(p, "Could not create temp directory");
        return p;
    }

    const std::string path = env.initrdTempDirPath;
    plan_run_cmd(p, std::string("EMBED_POPULATE_INITRD_DIR ") + path, false);

    if (path.rfind("/tmp/", 0) == 0) {
        plan_dir_remove_recursively(p, path + "/lib/modules");
    }

    {
        const std::string source = "/etc/initrd-release";
        const std::string dest = path + "/etc/initrd-release";
        if (env.initrdReleaseExists && env.initrdReleaseIsFile) {
            if (!env.initrdReleaseDestExists) {
                plan_file_copy(p, source, dest);
            } else {
                plan_file_remove(p, dest);
                plan_file_copy(p, source, dest);
            }
        }
    }
    {
        const std::string source = "/etc/initrd_release";
        const std::string dest = path + "/etc/initrd_release";
        if (env.initrd_releaseExists && env.initrd_releaseIsFile) {
            if (!env.initrd_releaseDestExists) {
                plan_file_copy(p, source, dest);
            } else {
                plan_file_remove(p, dest);
                plan_file_copy(p, source, dest);
            }
        }
    }

    plan_run_cmd(p,
                 std::string("/usr/share/") + env.applicationName
                     + "/scripts/copy-initrd-modules -e -t=\"" + path + "\" -k=\"" + settings.kernel + "\"",
                 false);
    plan_proc_root(p, "copy-initrd-programs", {"-e", "--to=" + path}, false);
    if (!env.loggedInUserName.empty()) {
        plan_proc_root(p, "chown", {"-R", env.loggedInUserName + ":", path}, true);
    }

    plan_chdir(p, path);
    plan_run_cmd(p,
                 std::string("(find . |cpio -o -H newc --owner root:root |gzip -9) >\"")
                     + (settings.workDir + "/iso-template/antiX/initrd.gz") + "\"",
                 false);
    plan_run_cmd(p,
                 std::string("MD5_CHECKSUM ") + settings.workDir + "/iso-template/antiX initrd.gz",
                 false);

    plan_temp_dir_remove(p, "initrd_dir");
    return p;
}

WorkCppPlan WorkCppPlanner::planSetupEnv(const SettingsCpp &settings, const SetupEnvEnv &env)
{
    WorkCppPlan p;

    const std::string bindRootPathForInstalledToLive = env.bindRootOverlayActive
                                                           ? std::string("/.bind-root")
                                                           : (env.setupBindRootOverlayOk
                                                                  ? (std::string("/run/") + env.applicationName
                                                                     + "/bind-root-overlay/root")
                                                                  : std::string("/.bind-root"));

    if (!env.workDirContainsS4Snapshot) {
        plan_abort(p, "setupEnv workDir does not contain s4-snapshot");
        return p;
    }

    // Work::setupEnv checks whether /boot is a mountpoint.
    plan_run_cmd(p, "mountpoint /boot", false);

    std::string bind_boot;
    std::string bind_boot_too;
    if (env.bootIsMountpoint) {
        bind_boot = "bind=/boot ";
        bind_boot_too = ",/boot";
    }

    if (settings.forceInstaller && env.needInstallCalamares) {
        plan_run_cmd(p, "INSTALL_PACKAGE calamares-settings-debian", false);
    }

    // writeSnapshotInfo()
    {
        plan_proc_root(p, "datetime_log", {}, true);
    }

    // writeVersionFile()
    {
        std::string filePath;
        if (!settings.dataFilesPath.empty()) {
            filePath = settings.dataFilesPath + "/files/etc/mx-version";
        } else {
            filePath = env.mxVersionFileExistsInUsrLocal
                           ? "/usr/local/share/live-files/files/etc/mx-version"
                           : "/usr/share/live-files/files/etc/mx-version";
        }
        plan_run_cmd(p,
                     std::string("WRITE_TEXT_FILE_UTF8_TRUNCATE ") + filePath + " "
                         + (settings.fullDistroName + " " + settings.codename + " " + settings.releaseDate + "\n"),
                     true);
    }

    // writeLsbRelease()
    {
        std::string filePath;
        if (!settings.dataFilesPath.empty()) {
            filePath = settings.dataFilesPath + "/files/etc/lsb-release";
        } else {
            filePath = env.lsbReleaseExistsInUsrLocal
                           ? "/usr/local/share/live-files/files/etc/lsb-release"
                           : "/usr/share/live-files/files/etc/lsb-release";
        }
        const std::string text = WorkCppUtils::buildLsbReleaseContent(
            settings.projectName,
            settings.distroVersion,
            settings.codename);
        plan_run_cmd(p,
                     std::string("WRITE_TEXT_FILE_UTF8_TRUNCATE ") + filePath + " " + text,
                     true);
    }

    // setupBindRootOverlay()
    {
        const std::string overlayBase = std::string("/run/") + env.applicationName + "/bind-root-overlay";
        const std::string lowerDir = overlayBase + "/lower";
        const std::string upperDir = overlayBase + "/upper";
        const std::string workDir = overlayBase + "/work";
        const std::string bindRoot = overlayBase + "/root";

        plan_proc_root(p, "mkdir", {"-p", overlayBase, lowerDir, upperDir, workDir, bindRoot}, true);

        plan_proc_root(p, "mountpoint", {"-q", bindRoot}, true);

        if (env.setupBindRootOverlay_bindRootIsMountpoint) {
            plan_proc_root(p, "umount", {"--recursive", bindRoot}, true);
        }

        plan_proc_root(p, "mountpoint", {"-q", lowerDir}, true);
        if (env.setupBindRootOverlay_lowerIsMountpoint) {
            plan_proc_root(p, "umount", {"--recursive", lowerDir}, true);
        }

        plan_proc_root(p, "mount", {"--bind", "/", lowerDir}, true);
        if (!env.setupBindRootOverlay_bindMountOk) {
            plan_message_box(p,
                             BoxType::critical,
                             "Error",
                             "Could not prepare a safe bind-root overlay. Snapshot cannot continue.");
            plan_abort(p, "setupBindRootOverlay failed: bind mount");
            plan_clean_up(p, settings, env, "setupBindRootOverlay failed");
            return p;
        }

        plan_proc_root(p,
                       "mount",
                       {"-t", "overlay", "overlay", "-o",
                        std::string("lowerdir=") + lowerDir + ",upperdir=" + upperDir + ",workdir=" + workDir,
                        bindRoot},
                       true);
        if (!env.setupBindRootOverlay_overlayMountOk) {
            plan_proc_root(p, "umount", {"--recursive", lowerDir}, true);
            plan_message_box(p,
                             BoxType::critical,
                             "Error",
                             "Could not prepare a safe bind-root overlay. Snapshot cannot continue.");
            plan_abort(p, "setupBindRootOverlay failed: overlay mount");
            plan_clean_up(p, settings, env, "setupBindRootOverlay failed");
            return p;
        }

        if (!env.setupBindRootOverlayOk) {
            plan_message_box(p,
                             BoxType::critical,
                             "Error",
                             "Could not prepare a safe bind-root overlay. Snapshot cannot continue.");
            plan_abort(p, "setupBindRootOverlay failed");
            plan_clean_up(p, settings, env, "setupBindRootOverlay failed");
            return p;
        }
    }

    // Setup environment if creating a respin (reset root/demo, remove personal accounts)
    plan_file_remove(p, "/var/lib/mxdebian/.mxsnapshot_accounts_reset.stp");

    if (settings.resetAccounts) {
        plan_proc_root(p, "mkdir", {"-p", "/var/lib/mxdebian/"}, true);
        plan_proc_root(p, "touch", {"-p", "/var/lib/mxdebian/.mxsnapshot_accounts_reset.stp"}, true);

        std::vector<std::string> args {"-F", "-b", bindRootPathForInstalledToLive};
        if (!settings.dataFilesPath.empty()) {
            args.push_back("-t");
            args.push_back(settings.dataFilesPath);
        }
        args.push_back("start");
        if (!bind_boot.empty()) {
            args.push_back("bind=/boot");
        }
        args.push_back("empty=/home");
        args.push_back("general");
        args.push_back("version-file");
        args.push_back("grubdefault");
        args.push_back("resumedisable");
        args.push_back("tdmnoautologin");
        args.push_back("sddmnoautologin");

        plan_proc_root(p, "installed-to-live", args, false);
        plan_run_cmd(p,
                     "CHECK_RESULT installed-to-live resetAccounts else ERROR: Could not prepare the snapshot bind-root environment.",
                     true);
    } else {
        std::vector<std::string> args {"-F", "-b", bindRootPathForInstalledToLive};
        if (!settings.dataFilesPath.empty()) {
            args.push_back("-t");
            args.push_back(settings.dataFilesPath);
        }
        args.push_back("start");
        args.push_back(std::string("bind=/home") + bind_boot_too);
        args.push_back("live-files");
        args.push_back("version-file");
        args.push_back("adjtime");
        args.push_back("grubdefault");
        args.push_back("resumedisable");

        plan_proc_root(p, "installed-to-live", args, false);
        plan_run_cmd(p,
                     "CHECK_RESULT installed-to-live normal else ERROR: Could not prepare the snapshot bind-root environment.",
                     true);
    }
    
    // If CHECK_RESULT failed, cleanup will be called and execution will abort
    // The following steps are only executed if installed-to-live succeeded

    if (!env.bindRootOverlayActive && !env.setupBindRootOverlayOk) {
        plan_proc_root(p,
                       "installed-to-live",
                       {"-b", bindRootPathForInstalledToLive, "read-only"},
                       true);
    }

    return p;
}


WorkCppPlan WorkCppPlanner::planCleanup(const SettingsCpp & /*settings*/, const CleanupEnv &env)
{
    WorkCppPlan p;

    // Step 1: chown_conf - restore file ownership
    plan_proc_root(p, "chown_conf", {}, true);

    // Step 2: Handle early exit if not started
    if (!env.started) {
        plan_temp_dir_remove(p, "initrd_dir");
        if (env.bindRootOverlayBaseNonEmpty) {
            plan_proc_root(p, "cleanup_overlay", {env.applicationName}, true);
        }
        plan_abort(p, "cleanUp exit: not started");
        return p;
    }

    // Step 3: Show "Cleaning..." message
    plan_message(p, "Cleaning...");

    // Step 4: Restore cursor visibility (write escape sequence to stdout)
    {
        WorkCppPlanStep st;
        st.payload = WorkCppPlanStep::RunCommandLine{"STDIO_WRITE_STDOUT \\033[?25h", true};
        p.steps.push_back(std::move(st));
    }

    // Step 5: Flush stdout
    {
        WorkCppPlanStep st;
        st.payload = WorkCppPlanStep::RunCommandLine{"STDIO_FLUSH_STDOUT", true};
        p.steps.push_back(std::move(st));
    }

    // Step 6: Kill mksquashfs processes
    plan_proc_root(p, "kill_mksquashfs", {}, true);

    // Step 7: Sync filesystem
    plan_process_execute(p, "sync", {}, -1);

    // Step 8: Change to root directory
    plan_chdir(p, "/");

    // Step 9: Run cleanup script if cleanup.conf exists
    if (env.cleanupConfExists) {
        plan_proc_root(p, "cleanup", {}, false);
    }

    // Step 10: Cleanup bind-root overlay
    if (env.bindRootOverlayBaseNonEmpty) {
        plan_proc_root(p, "cleanup_overlay", {env.applicationName}, true);
    }

    // Step 11: Remove accounts reset marker file
    plan_file_remove(p, "/var/lib/mxdebian/.mxsnapshot_accounts_reset.stp");

    // Step 12: Remove initrd temp directory
    plan_temp_dir_remove(p, "initrd_dir");

    // Step 13: Reset tmpdir (handled by TempDirRemove for settings->tmpdir)
    {
        WorkCppPlanStep st;
        st.payload = WorkCppPlanStep::TempDirRemove{"tmpdir"};
        p.steps.push_back(std::move(st));
    }

    // Step 14: Handle completion based on done flag
    if (env.done) {
        // Success path
        plan_message(p, "Done");
        plan_proc_root(p, "copy_log", {}, true);
        
        if (env.shutdownRequested) {
            plan_file_copy(p,
                           std::string("/tmp/") + env.applicationName + ".log",
                           env.snapshotDir + "/" + env.snapshotName + ".log");
            plan_process_execute(p, "sync", {}, -1);
            plan_process_execute(p, "shutdown", {"-h", "now"}, 0);
        }
        plan_abort(p, "cleanUp exit: success");
    } else {
        // Failure path
        plan_message(p, "Interrupted or failed to complete");
        plan_proc_root(p, "copy_log", {}, true);
        plan_abort(p, "cleanUp exit: failure");
    }

    return p;
}
