/**********************************************************************
 *  work.cpp
 **********************************************************************
 * Copyright (C) 2020-2025 MX Authors
 *
 * Authors: Adrian
 *          Debian <http://debian.org>
 *
 * This file is part of S4 Snapshot.
 *
 * S4 Snapshot is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * S4 Snapshot is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with S4 Snapshot.  If not, see <http://www.gnu.org/licenses/>.
 **********************************************************************/

#include "work.h"

#include <QCoreApplication>
#include <QDebug>
#include <QObject>
#include <QTime>

#include <algorithm>
#include <stdexcept>

#include "command_runner.h"
#include "process_runner.h"
#include "settings.h"
#include "file_cpp.h"
#include "dir_cpp.h"
#include "filesystemutils_cpp.h"
#include "stdio_cpp.h"
#ifdef CLI_BUILD
#include "messagehandler_cpp.h"
#else
#include "messagehandler.h"
#endif
#include "work_cpp_utils.h"
#include "tempfile_cpp.h"

namespace
{
std::vector<std::string> toStdVector(const QStringList &list)
{
    std::vector<std::string> out;
    out.reserve(static_cast<size_t>(list.size()));
    for (const QString &s : list) {
        out.push_back(s.toStdString());
    }

    return out;
}

static bool glob_qt_default_match_charclass(QStringView klass, QChar ch)
{
    qsizetype i = 0;
    bool negated = false;
    if (i < klass.size() && klass.at(i) == QLatin1Char('!')) {
        negated = true;
        ++i;
    }

    bool matched = false;
    if (i < klass.size() && klass.at(i) == QLatin1Char(']')) {
        matched = (ch == QLatin1Char(']'));
        ++i;
    }

    while (i < klass.size()) {
        const QChar c1 = klass.at(i);
        if (i + 2 < klass.size() && klass.at(i + 1) == QLatin1Char('-')) {
            const QChar c2 = klass.at(i + 2);
            const char16_t a = c1.unicode();
            const char16_t b = c2.unicode();
            const char16_t x = ch.unicode();
            if (a <= b) {
                if (a <= x && x <= b) {
                    matched = true;
                }
            } else {
                if (b <= x && x <= a) {
                    matched = true;
                }
            }
            i += 3;
        } else {
            if (ch == c1) {
                matched = true;
            }
            ++i;
        }
    }

    return negated ? !matched : matched;
}

static bool glob_qt_default_match_component_impl(QStringView pattern, qsizetype pi, QStringView text, qsizetype ti)
{
    while (pi < pattern.size()) {
        const QChar pc = pattern.at(pi);
        if (pc == QLatin1Char('*')) {
            while (pi < pattern.size() && pattern.at(pi) == QLatin1Char('*')) {
                ++pi;
            }
            if (pi >= pattern.size()) {
                return text.indexOf(QLatin1Char('/'), ti) < 0;
            }
            qsizetype maxK = text.indexOf(QLatin1Char('/'), ti);
            if (maxK < 0) {
                maxK = text.size();
            }
            for (qsizetype k = ti; k <= maxK; ++k) {
                if (glob_qt_default_match_component_impl(pattern, pi, text, k)) {
                    return true;
                }
            }
            return false;
        }

        if (ti >= text.size()) {
            return false;
        }

        if (pc == QLatin1Char('?')) {
            if (text.at(ti) == QLatin1Char('/')) {
                return false;
            }
            ++pi;
            ++ti;
            continue;
        }

        if (pc == QLatin1Char('[')) {
            const qsizetype closePos = pattern.indexOf(QLatin1Char(']'), pi + 1);
            if (closePos < 0) {
                return false;
            }
            const QStringView klass = pattern.mid(pi + 1, closePos - (pi + 1));
            const QChar ch = text.at(ti);
            if (ch == QLatin1Char('/')) {
                return false;
            }
            if (!glob_qt_default_match_charclass(klass, ch)) {
                return false;
            }
            pi = closePos + 1;
            ++ti;
            continue;
        }

        if (text.at(ti) != pc) {
            return false;
        }
        ++pi;
        ++ti;
    }

    return ti == text.size();
}

static bool glob_qt_default_match_component(QStringView pattern, QStringView text)
{
    qsizetype i = 0;
    while (i < pattern.size()) {
        const QChar c = pattern.at(i);
        if (c == QLatin1Char('[')) {
            const qsizetype closePos = pattern.indexOf(QLatin1Char(']'), i + 1);
            if (closePos < 0) {
                return false;
            }
            for (qsizetype k = i + 1; k < closePos; ++k) {
                const QChar cc = pattern.at(k);
                if (cc == QLatin1Char('/')) {
                    return false;
                }
            }
            i = closePos + 1;
            continue;
        }
        ++i;
    }
    return glob_qt_default_match_component_impl(pattern, 0, text, 0);
}

[[maybe_unused]] CommandRunner::QuietMode toQuietMode(bool quietYes)
{
    return quietYes ? CommandRunner::QuietMode::Yes : CommandRunner::QuietMode::No;
}

[[maybe_unused]] bool runCommandLine(const QString &cmd, CommandRunner::QuietMode quiet = CommandRunner::QuietMode::No)
{
    return CommandRunner::run(cmd.toStdString(), quiet);
}

[[maybe_unused]] CommandRunner::Result proc(const QString &program, const QStringList &args, const QByteArray *input,
                                            CommandRunner::QuietMode quiet = CommandRunner::QuietMode::No)
{
    const std::string stdinText = (input && !input->isEmpty())
        ? std::string(input->constData(), static_cast<size_t>(input->size()))
        : std::string();
    return CommandRunner::proc(program.toStdString(), toStdVector(args), stdinText, quiet, CommandRunner::Elevation::No);
}

[[maybe_unused]] CommandRunner::Result procAsRoot(const QString &program, const QStringList &args, const QByteArray *input,
                                                  CommandRunner::QuietMode quiet = CommandRunner::QuietMode::No)
{
    const std::string stdinText = (input && !input->isEmpty())
        ? std::string(input->constData(), static_cast<size_t>(input->size()))
        : std::string();
    return CommandRunner::procAsRoot(program.toStdString(), toStdVector(args), stdinText, quiet);
}

[[maybe_unused]] std::string getOut(const QString &cmd, CommandRunner::QuietMode quiet = CommandRunner::QuietMode::No)
{
    return CommandRunner::getOut(cmd.toStdString(), quiet);
}

[[maybe_unused]] std::string getOutAsRoot(const QString &program, const QStringList &args,
                                          CommandRunner::QuietMode quiet = CommandRunner::QuietMode::No)
{
    return CommandRunner::getOutAsRoot(program.toStdString(), toStdVector(args), quiet);
}

QString loggedInUserName()
{
    return QString::fromStdString(CommandRunner::loggedInUserName());
}

QStringList splitShellWords(const QString &text)
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

} // namespace

Work::Work(Settings *settings)
    : settings(settings)
{
    if (!settings) {
        qCritical() << "Work constructor: Settings pointer cannot be null";
        throw std::invalid_argument("Settings pointer cannot be null");
    }

    qDebug() << "Work object initialized for settings:" << settings->snapshotName;
}

qint64 Work::getElapsedTime() const
{
    if (!started) {
        return 0;
    }
    const auto now = std::chrono::steady_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_timerStart).count();
    return static_cast<qint64>(ms);
}

void Work::notifyMessage(const QString &msg)
{
    if (m_onMessage) {
        m_onMessage(msg);
    }
}

void Work::notifyMessageBox(BoxType box_type, const QString &title, const QString &msg)
{
    if (m_onMessageBox) {
        m_onMessageBox(box_type, title, msg);
    }
}

bool Work::isEnvironmentReady() const
{
    return WorkCppUtils::isEnvironmentReady(settings->workDir.toStdString(), settings->snapshotDir.toStdString());
}

// Checks if there's enough space on partitions, if not post error, cleanup and exit
// For installed systems we account for /home when on a separate partition.
void Work::checkEnoughSpace()
{
    quint64 required_space = getRequiredSpace();
    // Check foremost if enough space for ISO on snapshot_dir, print error and exit if not
    checkNoSpaceAndExit(required_space, settings->freeSpace, settings->snapshotDir);

    /* If snapshot and workdir are on the same partition we need about double the size of required_space.
     * If both TMP work_dir and ISO don't fit on snapshot_dir, see if work_dir can be put on /home or /tmp
     * we already checked that ISO can fit on snapshot_dir so if TMP work fits on /home or /tmp move
     * the work_dir to the appropriate place and return */
    if (FileSystemUtilsCpp::deviceId((settings->workDir + "/").toStdString())
        == FileSystemUtilsCpp::deviceId((settings->snapshotDir + "/").toStdString())) {
        if (settings->freeSpace < required_space * 2) {
            if (checkAndMoveWorkDir("/tmp", required_space)) {
                return;
            }
            if (checkAndMoveWorkDir("/home", required_space)) {
                return;
            }
            checkNoSpaceAndExit(required_space * 2, settings->freeSpace,
                                settings->snapshotDir); // Print out error and exit
        }
    } else { // If not on the same partitions, check if work_dir has enough free space for temp files for required_space
        checkNoSpaceAndExit(required_space, settings->freeSpaceWork, settings->workDir);
    }
}

bool Work::checkInstalled(const QString &package)
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";

    // Validate package name contains only safe characters to prevent command injection
    // Debian package names allow: lowercase letters, digits, plus, minus, dot.
    // Also allow optional architecture qualifiers like ":amd64".
    const auto isValidPackageName = [](const QString &s) -> bool {
        if (s.isEmpty()) {
            return false;
        }
        for (qsizetype i = 0; i < s.size(); ++i) {
            const QChar ch = s.at(i);
            const ushort u = ch.unicode();
            const bool ok = (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9') || u == '+' || u == '.' || u == ':' || u == '-' ;
            if (!ok) {
                return false;
            }
        }
        return true;
    };
    if (!isValidPackageName(package)) {
        qWarning() << "Invalid package name:" << package;
        return false;
    }

    return WorkCppUtils::checkInstalled(package.toStdString());
}

void Work::cleanUp()
{
    if (!started) {
        initrd_dir.remove();
        cleanupBindRootOverlay();
        exit(EXIT_SUCCESS);
    }

    (void)procAsRoot("chown_conf", {}, nullptr, CommandRunner::QuietMode::Yes);
    notifyMessage(QObject::tr("Cleaning..."));
    (void)StdioCpp::write(stdout, "\033[?25h");
    (void)StdioCpp::flush(stdout);
    (void)procAsRoot("kill_mksquashfs", {}, nullptr, CommandRunner::QuietMode::Yes);
    (void)ProcessRunner::execute("sync", {});
    (void)DirCpp::setCurrent("/");
    if (FileCpp::exists("/tmp/installed-to-live/cleanup.conf")) {
        (void)procAsRoot("cleanup", {}, nullptr, CommandRunner::QuietMode::No);
    }
    cleanupBindRootOverlay();
    (void)FileCpp::remove("/var/lib/mxdebian/.mxsnapshot_accounts_reset.stp");
    initrd_dir.remove();
    settings->tmpdir.reset();
    if (done) {
        notifyMessage(QObject::tr("Done"));
        (void)procAsRoot("copy_log", {}, nullptr, CommandRunner::QuietMode::Yes);
        if (settings->shutdown) {
            (void)FileCpp::copy(("/tmp/" + QCoreApplication::applicationName() + ".log").toStdString(),
                                (settings->snapshotDir + "/" + settings->snapshotName + ".log").toStdString());
            (void)ProcessRunner::execute("sync", {});
            (void)ProcessRunner::execute("shutdown", {"-h", "now"}, 0);
        }
        exit(EXIT_SUCCESS);
    }

    notifyMessage(QObject::tr("Interrupted or failed to complete"));
    (void)procAsRoot("copy_log", {}, nullptr, CommandRunner::QuietMode::Yes);
    exit(EXIT_FAILURE);
}

// Check if we can put work_dir on another partition with enough space, move work_dir there and setupEnv again
bool Work::checkAndMoveWorkDir(const QString &dir, quint64 req_size)
{
    // See first if the dir is on different partition otherwise it's irrelevant
    if (FileSystemUtilsCpp::deviceId((dir + "/").toStdString())
            != FileSystemUtilsCpp::deviceId((settings->snapshotDir + "/").toStdString())
        && FileSystemUtilsCpp::getFreeSpaceKiB(dir.toStdString()) > req_size) {
        if (FileCpp::exists("/tmp/installed-to-live/cleanup.conf")) {
            const QString snapshotLib = "/usr/lib/" + QCoreApplication::applicationName() + "/snapshot-lib";
            const QString elevateTool2 = QString::fromStdString(CommandRunner::elevationTool());
            (void)runCommandLine(elevateTool2 + " " + snapshotLib + " cleanup");
        }
        settings->tempDirParent = dir;
        if (!settings->checkTempDir()) {
            cleanUp();
        }
        setupEnv();
        return true;
    }
    return false;
}

void Work::checkNoSpaceAndExit(quint64 needed_space, quint64 free_space, const QString &dir)
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    constexpr float factor = 1024 * 1024;
    constexpr double kibToMib = 1024.0;
    qDebug().noquote() << "Needed space:" << QString::number(needed_space / kibToMib, 'f', 2) << "MiB";
    qDebug().noquote() << "Free space  :" << QString::number(free_space / kibToMib, 'f', 2) << "MiB on" << dir;
    if (needed_space > free_space) {
        notifyMessageBox(
            BoxType::critical, QObject::tr("Error"),
            QObject::tr("There's not enough free space on your target disk, you need at least %1")
                    .arg(QString::number(static_cast<double>(needed_space) / factor, 'f', 2) + "GiB")
                + "\n"
                + QObject::tr("You have %1 free space on %2")
                      .arg(QString::number(static_cast<double>(free_space) / factor, 'f', 2) + "GiB", dir)
                + "\n"
                + QObject::tr("If you are sure you have enough free space rerun the program with -o/--override-size option"));
        cleanUp();
    }
}

bool Work::setupBindRootOverlay()
{
    const QString appName = QCoreApplication::applicationName();
    const QString overlayBase = "/run/" + appName + "/bind-root-overlay";
    const QString lowerDir = overlayBase + "/lower";
    const QString upperDir = overlayBase + "/upper";
    const QString workDir = overlayBase + "/work";
    const QString bindRoot = overlayBase + "/root";

    bindRootOverlayActive = false;
    bindRootOverlayBase.clear();
    bindRootPath = "/.bind-root";

    (void)procAsRoot("mkdir", {"-p", overlayBase, lowerDir, upperDir, workDir, bindRoot}, nullptr,
                    CommandRunner::QuietMode::Yes);

    {
        const CommandRunner::Result mp = procAsRoot("mountpoint", {"-q", bindRoot}, nullptr, CommandRunner::QuietMode::Yes);
        if (mp.started && mp.normalExit && mp.exitCode == 0) {
            (void)procAsRoot("umount", {"--recursive", bindRoot}, nullptr, CommandRunner::QuietMode::Yes);
        }
    }
    {
        const CommandRunner::Result mp = procAsRoot("mountpoint", {"-q", lowerDir}, nullptr, CommandRunner::QuietMode::Yes);
        if (mp.started && mp.normalExit && mp.exitCode == 0) {
            (void)procAsRoot("umount", {"--recursive", lowerDir}, nullptr, CommandRunner::QuietMode::Yes);
        }
    }

    {
        const CommandRunner::Result m = procAsRoot("mount", {"--bind", "/", lowerDir}, nullptr, CommandRunner::QuietMode::Yes);
        if (!(m.started && m.normalExit && m.exitCode == 0)) {
        qWarning() << "Failed to bind mount / to" << lowerDir;
        return false;
        }
    }

    const QString overlayOptions = "lowerdir=" + lowerDir + ",upperdir=" + upperDir + ",workdir=" + workDir;
    {
        const CommandRunner::Result m = procAsRoot(
            "mount", {"-t", "overlay", "overlay", "-o", overlayOptions, bindRoot}, nullptr, CommandRunner::QuietMode::Yes);
        if (!(m.started && m.normalExit && m.exitCode == 0)) {
        qWarning() << "Failed to mount overlay at" << bindRoot;
        (void)procAsRoot("umount", {"--recursive", lowerDir}, nullptr, CommandRunner::QuietMode::Yes);
        return false;
        }
    }

    bindRootPath = bindRoot;
    bindRootOverlayBase = overlayBase;
    bindRootOverlayActive = true;
    return true;
}

void Work::cleanupBindRootOverlay()
{
    if (bindRootOverlayBase.isEmpty()) {
        bindRootOverlayActive = false;
        bindRootPath = "/.bind-root";
        return;
    }
    (void)procAsRoot("cleanup_overlay", {QCoreApplication::applicationName()}, nullptr, CommandRunner::QuietMode::Yes);
    bindRootOverlayActive = false;
    bindRootOverlayBase.clear();
    bindRootPath = "/.bind-root";
}

void Work::closeInitrd(const QString &initrd_dir, const QString &file)
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    (void)DirCpp::setCurrent(initrd_dir.toStdString());
    (void)runCommandLine("(find . |cpio -o -H newc --owner root:root |gzip -9) >\"" + file + "\"");
    makeChecksum(HashType::md5, settings->workDir + "/iso-template/antiX", "initrd.gz");
} // Added closing brace here

// copyModules(mod_dir/kernel kernel)
void Work::copyModules(const QString &to, const QString &kernel)
{
    (void)runCommandLine(QString(R"(/usr/share/%1/scripts/copy-initrd-modules -e -t=\"%2\" -k=\"%3\")")
                             .arg(qApp->applicationName(), to, kernel));
    (void)procAsRoot("copy-initrd-programs", {"-e", "--to=" + to}, nullptr, CommandRunner::QuietMode::No);
    const QString username = loggedInUserName();
    if (!username.isEmpty()) {
        (void)procAsRoot("chown", {"-R", username + ":", to}, nullptr, CommandRunner::QuietMode::Yes);
    }
}

// Copying the iso-template filesystem
void Work::copyNewIso()
{
    notifyMessage(QObject::tr("Copying the new-iso filesystem..."));
    (void)DirCpp::setCurrent(settings->workDir.toStdString());

    // Use multi-init template if available and both init systems are installed
    if (FileCpp::exists("/usr/lib/iso-template/iso-template-multi.tar.gz")
        && FileCpp::exists("/usr/lib/sysvinit/init") && FileCpp::exists("/usr/lib/systemd/systemd")) {
        (void)runCommandLine("tar xf /usr/lib/iso-template/iso-template-multi.tar.gz");
    } else {
        (void)runCommandLine("tar xf /usr/lib/iso-template/iso-template.tar.gz");
    }
    (void)runCommandLine("cp /usr/lib/iso-template/template-initrd.gz iso-template/antiX/initrd.gz");
    (void)runCommandLine("cp /boot/vmlinuz-" + settings->kernel + " iso-template/antiX/vmlinuz");

    replaceMenuStrings();
    makeChecksum(HashType::md5, settings->workDir + "/iso-template/antiX", "vmlinuz");

    QString path = QString::fromLocal8Bit(initrd_dir.path().c_str());
    if (!initrd_dir.isValid()) {
        qDebug() << QObject::tr("Could not create temp directory. ") + path;
        cleanUp();
    }

    openInitrd(settings->workDir + "/iso-template/antiX/initrd.gz", path);

    // Strip modules; make sure initrd_dir is correct to avoid disaster
    if (path.startsWith("/tmp/")) {
        const DirCpp modulesDir((path + "/lib/modules").toStdString());
        if (modulesDir.exists()) {
            (void)modulesDir.removeRecursively();
        }
    }

    // For old versions we copy initrd-release for newer ones we copy initrd_release
    QString sourcePath = "/etc/initrd-release";
    QString destinationPath = path + "/etc/initrd-release";
    if (FileCpp::exists(sourcePath.toStdString()) && FileCpp::isFile(sourcePath.toStdString())) {
        if (!FileCpp::exists(destinationPath.toStdString()) || FileCpp::remove(destinationPath.toStdString())) {
            (void)FileCpp::copy(sourcePath.toStdString(), destinationPath.toStdString());
        }
    }
    sourcePath = "/etc/initrd_release";
    destinationPath = path + "/etc/initrd_release";
    if (FileCpp::exists(sourcePath.toStdString()) && FileCpp::isFile(sourcePath.toStdString())) {
        if (!FileCpp::exists(destinationPath.toStdString()) || FileCpp::remove(destinationPath.toStdString())) {
            (void)FileCpp::copy(sourcePath.toStdString(), destinationPath.toStdString());
        }
    }

    if (initrd_dir.isValid()) {
        copyModules(path, settings->kernel);
        closeInitrd(path, settings->workDir + "/iso-template/antiX/initrd.gz");
        initrd_dir.remove();
    }
}

// Create squashfs and then the iso
bool Work::createIso(const QString &filename)
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    // Squash the filesystem copy
    const bool useUnbuffer = checkInstalled("expect");
    using Release::Version;
    QStringList squashfsArgs {bindRootPath, settings->workDir + "/iso-template/antiX/linuxfs",
                              "-comp", settings->compression,
                              "-processors", QString::number(settings->cores)};
    if (Settings::getDebianVerNum() >= Version::Bookworm) {
        squashfsArgs << "-throttle" << QString::number(settings->throttle);
    }
    squashfsArgs += splitShellWords(settings->mksqOpt);
    squashfsArgs << "-wildcards" << "-ef" << settings->snapshotExcludesPath;
    const QStringList sessionExcludes = splitShellWords(settings->sessionExcludes);
    if (!sessionExcludes.isEmpty()) {
        squashfsArgs << "-e";
        squashfsArgs += sessionExcludes;
    }

    QString wrapperCommand = useUnbuffer ? "unbuffer" : "stdbuf";
    QStringList wrapperArgs;
    if (!useUnbuffer) {
        wrapperArgs << "-o0";
    }
    wrapperArgs << "mksquashfs";
    wrapperArgs += squashfsArgs;

    notifyMessage(QObject::tr("Squashing filesystem..."));
    const CommandRunner::Result squash = procAsRoot(wrapperCommand, wrapperArgs, nullptr, CommandRunner::QuietMode::No);
    if (!(squash.started && squash.normalExit && squash.exitCode == 0)) {
        notifyMessageBox(
            BoxType::critical, QObject::tr("Error"),
            QObject::tr("Could not create linuxfs file, please check /var/log/%1.log").arg(QCoreApplication::applicationName()));
        return false;
    }
    writeUnsquashfsSize(QString::fromStdString(squash.mergedText).trimmed());

    // Move linuxfs files to iso-2/antiX folder
    (void)DirCpp().mkpath("iso-2/antiX");
    (void)runCommandLine("mv iso-template/antiX/linuxfs* iso-2/antiX");
    makeChecksum(HashType::md5, settings->workDir + "/iso-2/antiX", "linuxfs");

    const QString snapshotLib = "/usr/lib/" + QCoreApplication::applicationName() + "/snapshot-lib";
    const QString elevateTool = QString::fromStdString(CommandRunner::elevationTool());
    (void)runCommandLine(elevateTool + " " + snapshotLib + " cleanup");

    // Create the iso file
    (void)DirCpp::setCurrent((settings->workDir + "/iso-template").toStdString());
    QString cmd
        = "xorriso -as mkisofs -l -V DEBIANLIVE -R -J -pad -iso-level 3 -no-emul-boot -boot-load-size 4 -boot-info-table "
          "-b boot/isolinux/isolinux.bin -eltorito-alt-boot -e boot/grub/efi.img -no-emul-boot -c "
          "boot/isolinux/isolinux.cat -o \""
          + settings->snapshotDir + "/" + filename + "\" . \"" + settings->workDir + "/iso-2\"";
    if (QString::fromStdString(CommandRunner::getOut("umask", CommandRunner::QuietMode::Yes)) != "0022") {
        cmd.prepend("umask 022; ");
    }
    notifyMessage(QObject::tr("Creating CD/DVD image file..."));
    if (!runCommandLine(cmd)) {
        notifyMessageBox(
            BoxType::critical, QObject::tr("Error"),
            QObject::tr("Could not create ISO file, please check whether you have enough space on the destination partition."));
        return false;
    }

    // Make it isohybrid
    if (settings->makeIsohybrid) {
        notifyMessage(QObject::tr("Making hybrid iso"));
        (void)runCommandLine("isohybrid --uefi \"" + settings->snapshotDir + "/" + filename + "\"");
    }

    // Make ISO checksums
    if (settings->makeMd5sum) {
        makeChecksum(HashType::md5, settings->snapshotDir, filename);
    }
    if (settings->makeSha512sum) {
        makeChecksum(HashType::sha512, settings->snapshotDir, filename);
    }

    auto elapsedTime = QTime(0, 0).addMSecs(static_cast<int>(getElapsedTime()));
    notifyMessage(QObject::tr("Done"));
    if (settings->shutdown) {
        done = true;
        cleanUp();
    }
    notifyMessageBox(BoxType::information, QObject::tr("Success"),
                    QObject::tr("Debian Snapshot completed successfully!") + '\n'
                        + QObject::tr("Snapshot took %1 to finish.").arg(elapsedTime.toString("hh:mm:ss")) + "\n\n"
                        + QObject::tr("Thanks for using Debian Snapshot, run Live USB Maker next!"));
    done = true;
    return true;
}

bool Work::installPackage(const QString &package)
{
    notifyMessage(QObject::tr("Installing ") + package);
    (void)procAsRoot("apt-get", {"update"}, nullptr, CommandRunner::QuietMode::No);
    bool cmd1res = false;
    if(package.contains("calamares-settings-debian")) {
      {
          const CommandRunner::Result r = procAsRoot(
              "apt-get", {"install", "-y", "-o DPkg::Options::=--force-confnew", package}, nullptr, CommandRunner::QuietMode::No);
          if (r.started && r.normalExit && r.exitCode == 0) cmd1res = true;
      }
    } else {
      {
          const CommandRunner::Result r = procAsRoot(
              "apt-get", {"install", "-y", package}, nullptr, CommandRunner::QuietMode::No);
          if (r.started && r.normalExit && r.exitCode == 0) cmd1res = true;
      }
    }
    if(! cmd1res) {
        notifyMessageBox(BoxType::critical, QObject::tr("Error"), QObject::tr("Could not install ") + package);
        return false;
    }
    return true;
}

// Convert HashType enum to string
std::string Work::hashTypeToString(HashType type)
{
    switch (type) {
        case HashType::md5:
            return "md5";
        case HashType::sha512:
            return "sha512";
    }
    // Should never reach here, but for safety return md5 as default
    return "md5";
}

// Create checksums for different files
void Work::makeChecksum(Work::HashType hash_type, const QString &folder, const QString &file_name)
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    notifyMessage(QObject::tr("Calculating checksum..."));
    (void)runCommandLine("sync");
    (void)DirCpp::setCurrent(folder.toStdString());
    QString ce = QString::fromStdString(hashTypeToString(hash_type));
    QString cmd;
    QString checksum_cmd = QString("%1sum \"%2\">\"%3/%2.%1\"").arg(ce, file_name, folder);
    QString temp_dir {"/tmp/snapshot-checksum-temp"};
    QString checksum_tmp
        = QString(
              "TD=%1; KEEP=$TD/.keep; [ -d $TD ] || mkdir $TD ; FN=\"%2\"; CF=\"%3/${FN}.%4\"; cp $FN $TD/$FN; pushd "
              "$TD>/dev/null; %4sum $FN > $FN.%4 ; cp $FN.%4 $CF; popd >/dev/null ; [ -e $KEEP ] || rm -rf $TD")
              .arg(temp_dir, file_name, folder, ce);

    if (settings->preempt) {
        // Check free space available on /tmp
        (void)runCommandLine(QString("TF=%1/\"%2\"; [ -f \"$TF\" ] && rm -f \"$TF\"").arg(temp_dir, file_name));
        if (!runCommandLine(
                QString("DUF=$(du -BM \"%1\" |grep -oE '^[[:digit:]]+'); TDA=$(df -BM --output=avail /tmp |grep -oE "
                        "'^[[:digit:]]+'); ((TDA/10*8 >= DUF))")
                    .arg(file_name))) {
            settings->preempt = false;
        }
    }
    if (!settings->preempt) {
        cmd = checksum_cmd;
    } else {
        // Free pagecache
        (void)runCommandLine("sync; sleep 1");
        const QString snapshotLib = "/usr/lib/" + QCoreApplication::applicationName() + "/snapshot-lib";
        const QString elevateTool = QString::fromStdString(CommandRunner::elevationTool());
        (void)runCommandLine(elevateTool + " " + snapshotLib + " drop_caches");
        (void)runCommandLine("sleep 1");
        cmd = checksum_tmp;
    }
    (void)runCommandLine(cmd);
    (void)DirCpp::setCurrent(settings->workDir.toStdString());
}

void Work::openInitrd(const QString &file, const QString &initrd_dir)
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    notifyMessage(QObject::tr("Building new initrd..."));
    (void)runCommandLine("chmod a+rx \"" + initrd_dir + "\"");
    (void)DirCpp::setCurrent(initrd_dir.toStdString());
    (void)runCommandLine(QString("gunzip -c \"%1\" |cpio -idum").arg(file));
}

// Replace text in menu items in grub.cfg, syslinux.cfg, isolinux.cfg
void Work::replaceMenuStrings()
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    QString fullDistroNameSpace = settings->fullDistroName;
    fullDistroNameSpace.replace("_", " ");

    const QString grub_cfg {"/iso-template/boot/grub/grub.cfg"};
    replaceStringInFile("%DISTRO%", settings->projectName + "-" + settings->distroVersion,
                        settings->workDir + grub_cfg);
    replaceStringInFile("%DISTRO_NAME%", settings->projectName, settings->workDir + grub_cfg);
    replaceStringInFile("%FULL_DISTRO_NAME%", settings->fullDistroName, settings->workDir + grub_cfg);
    replaceStringInFile("%FULL_DISTRO_NAME_SPACE%", fullDistroNameSpace, settings->workDir + grub_cfg);
    replaceStringInFile("%RELEASE_DATE%", settings->releaseDate, settings->workDir + grub_cfg);

    const QString grubenv_cfg {"/iso-template/boot/grub/grubenv.cfg"};
    const QString boot_pararameter_regexp {"(lang|kbd|kbvar|kbopt|tz)=[^[:space:]]*"};
    (void)runCommandLine(QString("printf '%s\\n' %1 | grep -E '^%2' >> '%3'")
                  .arg(settings->bootOptions, boot_pararameter_regexp, settings->workDir + grubenv_cfg));
    (void)runCommandLine(
        QString(
            R"(sed -i "s|%OPTIONS%|$(sed -r 's/[[:space:]]%2/ /g; s/^[[:space:]]+//; s/[[:space:]]+/ /g'<<<' %1')|" '%3')")
            .arg(settings->bootOptions, boot_pararameter_regexp, settings->workDir + grub_cfg));
    const QString syslinux_cfg {"/iso-template/boot/syslinux/syslinux.cfg"};
    const QString isolinux_cfg {"/iso-template/boot/isolinux/isolinux.cfg"};
    for (const QString &file : {syslinux_cfg, isolinux_cfg}) {
        replaceStringInFile("%OPTIONS%", settings->bootOptions, settings->workDir + file);
        replaceStringInFile("%CODE_NAME%", settings->codename, settings->workDir + file);
    }

    const QString sys_readme = "/iso-template/boot/syslinux/readme.msg";
    const QString iso_readme = "/iso-template/boot/isolinux/readme.msg";
    const QStringList cfg_files {syslinux_cfg, isolinux_cfg, sys_readme, iso_readme};
    for (const QString &file : cfg_files) {
        replaceStringInFile("%FULL_DISTRO_NAME%", settings->fullDistroName, settings->workDir + file);
        replaceStringInFile("%RELEASE_DATE%", settings->releaseDate, settings->workDir + file);
    }

    {
        const std::string themeDir = (settings->workDir + "/iso-template/boot/grub/theme").toStdString();
        const std::vector<std::string> themeFiles = DirCpp(themeDir).entryInfoList({"*.txt"}, DirCpp::EntryType::Files);
        for (const std::string &themeFile : themeFiles) {
            const QString p = QString::fromLocal8Bit(themeFile.c_str());
            replaceStringInFile("%ASCII_CODE_NAME%", settings->codename, p);
            replaceStringInFile("%DISTRO%", settings->projectName + "-" + settings->distroVersion, p);
        }
    }
}

// Util function for replacing strings in files
bool Work::replaceStringInFile(const QString &old_text, const QString &new_text, const QString &file_path)
{
    const WorkCppUtils::ReplaceStringError e = WorkCppUtils::replaceStringInFileUtf8NoBom(
        file_path.toStdString(),
        old_text.toStdString(),
        new_text.toStdString());
    if (e == WorkCppUtils::ReplaceStringError::OpenFailed) {
        qWarning() << "Failed to open file:" << file_path;
        return false;
    }
    if (e == WorkCppUtils::ReplaceStringError::WriteFailed) {
        qWarning() << "Failed to write to file:" << file_path;
        return false;
    }
    return true;
}

// Save package list in working directory
void Work::savePackageList(const QString &file_name)
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    const QString fullName = QString("%1/iso-template/%2/package_list")
                                 .arg(settings->workDir, QString::fromStdString(FileCpp::completeBaseName(file_name.toStdString())));
    const QString cmd = QString(R"(dpkg -l | awk '/^ii /{printf "%-41s %s\n", $2, $3}' > '%1')").arg(fullName);
    (void)runCommandLine(cmd);
}

// Setup the environment before taking the snapshot
void Work::setupEnv()
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    // Checks if work_dir looks OK
    if (!settings->workDir.contains("/s4-snapshot")) {
        cleanUp();
    }

    QString bind_boot;
    QString bind_boot_too;
    if (runCommandLine("mountpoint /boot")) {
        bind_boot = "bind=/boot ";
        bind_boot_too = ",/boot";
    }

    // Install installer if absent
    if (settings->forceInstaller && !checkInstalled("calamares-settings-debian")) {
        installPackage("calamares-settings-debian");
    }

    writeSnapshotInfo();
    writeVersionFile();
    writeLsbRelease();

    if (!setupBindRootOverlay()) {
        notifyMessageBox(BoxType::critical, QObject::tr("Error"),
                        QObject::tr("Could not prepare a safe bind-root overlay. Snapshot cannot continue."));
        cleanUp();
    }

    // Setup environment if creating a respin (reset root/demo, remove personal accounts)
    (void)FileCpp::remove("/var/lib/mxdebian/.mxsnapshot_accounts_reset.stp");
    if (settings->resetAccounts) {
        QStringList args {"-F", "-b", bindRootPath, "start"};
        if (!bind_boot.isEmpty()) {
            args << "bind=/boot";
        }
        (void)procAsRoot("mkdir", {"-p", "/var/lib/mxdebian/"}, nullptr, CommandRunner::QuietMode::Yes);
        (void)procAsRoot("touch", {"-p", "/var/lib/mxdebian/.mxsnapshot_accounts_reset.stp"}, nullptr, CommandRunner::QuietMode::Yes);
        args << "empty=/home" << "general" << "version-file";
        args << "grubdefault" << "resumedisable" << "tdmnoautologin" << "sddmnoautologin";
        {
            const CommandRunner::Result r = procAsRoot("installed-to-live", args, nullptr, CommandRunner::QuietMode::No);
            if (!(r.started && r.normalExit && r.exitCode == 0)) {
            notifyMessageBox(BoxType::critical, QObject::tr("Error"),
                            QObject::tr("Could not prepare the snapshot bind-root environment."));
            cleanUp();
            }
        }
    } else {
        QStringList args {"-F", "-b", bindRootPath, "start", "bind=/home" + bind_boot_too,
                          "live-files", "version-file", "adjtime"};
        args << "grubdefault" << "resumedisable";
        {
            const CommandRunner::Result r = procAsRoot("installed-to-live", args, nullptr, CommandRunner::QuietMode::No);
            if (!(r.started && r.normalExit && r.exitCode == 0)) {
            notifyMessageBox(BoxType::critical, QObject::tr("Error"),
                            QObject::tr("Could not prepare the snapshot bind-root environment."));
            cleanUp();
            }
        }
    }
    if (!bindRootOverlayActive) {
        (void)procAsRoot("installed-to-live", {"-b", bindRootPath, "read-only"}, nullptr, CommandRunner::QuietMode::Yes);
    }
    //shell.runAsRoot("dash /usr/share/s4-snapshot/scripts/configure_debian_calamares.sh"); //configure calamares for use with s4 snapshot
}

void Work::writeLsbRelease()
{
    QString filePath = "/usr/local/share/live-files/files/etc/lsb-release";
    if (!FileCpp::exists(filePath.toStdString())) {
        filePath = "/usr/share/live-files/files/etc/lsb-release";
    }

    const std::string text = WorkCppUtils::buildLsbReleaseContent(settings->projectName.toStdString(),
                                                                  settings->distroVersion.toStdString(),
                                                                  settings->codename.toStdString());
    (void)WorkCppUtils::writeTextFileUtf8NoBomTruncate(filePath.toStdString(), text);
}

// Write date of the snapshot in a "snapshot_created" file
void Work::writeSnapshotInfo()
{
    const QString snapshotLib = "/usr/lib/" + QCoreApplication::applicationName() + "/snapshot-lib";
    const QString elevateTool = QString::fromStdString(CommandRunner::elevationTool());
    (void)runCommandLine(elevateTool + " " + snapshotLib + " datetime_log", CommandRunner::QuietMode::Yes);
}

void Work::writeVersionFile()
{
    QString filePath = "/usr/local/share/live-files/files/etc/mx-version";
    if (!FileCpp::exists(filePath.toStdString())) {
        filePath = "/usr/share/live-files/files/etc/mx-version";
    }
    const std::string text = (settings->fullDistroName + " " + settings->codename + " " + settings->releaseDate + "\n").toStdString();
    (void)WorkCppUtils::writeTextFileUtf8NoBomTruncate(filePath.toStdString(), text);
}

void Work::writeUnsquashfsSize(const QString &text)
{
    const QString sep = QStringLiteral(" uncompressed filesystem size (");
    QString value;
    const qsizetype pos = text.indexOf(sep);
    if (pos >= 0) {
        QString after = text.mid(pos + sep.size());
        const qsizetype secondPos = after.indexOf(sep);
        if (secondPos >= 0) {
            after.truncate(secondPos);
        }
        const qsizetype spacePos = after.indexOf(' ');
        value = (spacePos >= 0) ? after.left(spacePos) : after;
    }
    (void)WorkCppUtils::writeQSettingsNativeGeneralString(
        (settings->workDir + "/iso-template/antiX/linuxfs.info").toStdString(),
        std::string("UncompressedSizeKB"),
        value.toStdString());
}

quint64 Work::getRequiredSpace()
{
    QStringList excludes;

    // Open and read the excludes file
    if (!settings->snapshotExcludesPath.isEmpty()) {
        FileCpp f(settings->snapshotExcludesPath.toStdString());
        if (!f.open(FileCpp::OpenMode::ReadOnly | FileCpp::OpenMode::Text)) {
            qDebug() << "Could not open file: " << settings->snapshotExcludesPath;
        } else {
            while (true) {
                std::string lineStd = f.readLine();
                if (lineStd.empty()) {
                    break;
                }
                QString line = QString::fromLocal8Bit(lineStd.c_str()).trimmed();
                if (!line.startsWith('#') && !line.isEmpty() && !line.startsWith(".bind-root")) {
                    excludes << line;
                }
            }
            f.close();
        }
    }

    // Add session excludes
    if (!settings->sessionExcludes.isEmpty()) {
        QString sessionExcludes = settings->sessionExcludes;
        QStringList excludeList = sessionExcludes.split("\" \"");
        excludes.reserve(excludeList.size());
        for (QString exclude : excludeList) {
            exclude = exclude.replace('"', "").trimmed();
            excludes << exclude;
        }
    }
    QString sizeRoot = bindRootPath;
    if (bindRootOverlayActive) {
        const QString overlayLower = bindRootOverlayBase + "/lower";
        if (FileCpp::exists(overlayLower.toStdString())) {
            sizeRoot = overlayLower;
        }
    }
    QString sizeRootPrefix = sizeRoot;
    if (!sizeRootPrefix.endsWith('/')) {
        sizeRootPrefix += "/";
    }
    const quint64 sizeRootDevice = static_cast<quint64>(FileSystemUtilsCpp::deviceId(sizeRoot.toStdString()));
    const quint64 rootInfoDevice = static_cast<quint64>(FileSystemUtilsCpp::deviceId(std::string("/")));
    const quint64 homeInfoDevice = static_cast<quint64>(FileSystemUtilsCpp::deviceId(std::string("/home")));
    quint64 rootDevice = sizeRootDevice;
    bool includeHomeDevice = false;
    if (!settings->live) {
        rootDevice = rootInfoDevice;
        const bool homeIsMount = FileSystemUtilsCpp::isMountPoint(std::string("/home"));
        if (homeIsMount && homeInfoDevice != 0 && homeInfoDevice != rootDevice) {
            includeHomeDevice = true;
        }
    }
    const auto isBindMount = [](const QString &mountPoint) -> bool {
        FileCpp mounts(std::string("/proc/self/mounts"));
        if (!mounts.open(FileCpp::OpenMode::ReadOnly | FileCpp::OpenMode::Text)) {
            return false;
        }

        while (true) {
            std::string lineStd = mounts.readLine();
            if (lineStd.empty()) {
                break;
            }
            QString line = QString::fromLocal8Bit(lineStd.c_str());
            if (line.endsWith('\n')) {
                line.chop(1);
                if (line.endsWith('\r')) {
                    line.chop(1);
                }
            }
            const QStringList parts = line.split(' ', Qt::SkipEmptyParts);
            if (parts.size() < 4) {
                continue;
            }
            QString target = parts.at(1);
            target.replace(QStringLiteral("\\040"), QStringLiteral(" "));
            if (target != mountPoint) {
                continue;
            }
            const QStringList options = parts.at(3).split(',', Qt::SkipEmptyParts);
            return options.contains(QStringLiteral("bind")) || options.contains(QStringLiteral("rbind"));
        }
        mounts.close();
        return false;
    };
    // If /home is bind-mounted or reset (empty in overlay), exclude it from the size estimate.
    if (!settings->live && FileSystemUtilsCpp::isMountPoint(std::string("/home")) && homeInfoDevice == rootDevice) {
        bool shouldExclude = isBindMount(QStringLiteral("/home"));
        if (!shouldExclude && bindRootOverlayActive) {
            const QString overlayHome = bindRootOverlayBase + "/root/home";
            const DirCpp homeDir(overlayHome.toStdString());
            if (homeDir.exists() && homeDir.isEmpty()) {
                shouldExclude = true;
            }
        }
        if (shouldExclude) {
            excludes << QStringLiteral("home");
        }
    }
    const auto containsWildcard = [](const QString &path) -> bool {
        const QString wildcards = "*?[{";
        for (const QChar &wildcard : wildcards) {
            if (path.contains(wildcard)) {
                return true;
            }
        }
        return false;
    };
    const auto normalizeExclude = [](QString path) -> QString {
        QString collapsed;
        collapsed.reserve(path.size());
        for (qsizetype i = 0; i < path.size(); ++i) {
            const QChar ch = path.at(i);
            if (ch == '/') {
                collapsed.append('/');
                while (i + 1 < path.size() && path.at(i + 1) == '/') {
                    ++i;
                }
            } else {
                collapsed.append(ch);
            }
        }
        path = collapsed;
        if (path.size() > 1 && path.endsWith('/')) {
            path.chop(1);
        }
        return path;
    };
    const QString sizeRootBase = sizeRootPrefix.endsWith('/') ? sizeRootPrefix.left(sizeRootPrefix.size() - 1)
                                                              : sizeRootPrefix;
    const auto isAllowedDevice = [&](const QString &path) -> bool {
        QString probePath = path;
        if (FileCpp::isSymLink(path.toStdString())) {
            probePath = QString::fromStdString(DirCpp::absolutePathOfContainingDir(path.toStdString()));
        }
        const quint64 probeDevice = static_cast<quint64>(FileSystemUtilsCpp::deviceId(probePath.toStdString()));
        return probeDevice == sizeRootDevice || probeDevice == rootInfoDevice || probeDevice == homeInfoDevice;
    };
    // Expand patterns without shell globbing or symlinked intermediate traversal.
    const auto expandExcludePattern = [&](QString rawPattern) -> QStringList {
        if (rawPattern.startsWith('/')) {
            rawPattern.remove(0, 1);
        }
        {
            if (rawPattern.endsWith(QLatin1String("/*"))) {
                rawPattern.chop(2);
            }
        }
        QString fullPattern = QString::fromLocal8Bit(DirCpp(sizeRootPrefix.toStdString()).filePath(rawPattern.toStdString()).c_str());
        fullPattern = QString::fromLocal8Bit(DirCpp::cleanPath(fullPattern.toStdString()).c_str());
        QString relativePattern = fullPattern;
        if (relativePattern.startsWith(sizeRootBase)) {
            relativePattern.remove(0, sizeRootBase.size());
        }
        if (relativePattern.startsWith('/')) {
            relativePattern.remove(0, 1);
        }
        if (relativePattern.isEmpty()) {
            return {sizeRootBase};
        }
        QStringList components = relativePattern.split('/', Qt::SkipEmptyParts);
        QStringList current {sizeRootBase};
        for (int i = 0; i < components.size(); ++i) {
            const QString &component = components.at(i);
            const bool isLast = (i == components.size() - 1);
            QStringList next;
            if (containsWildcard(component)) {
                for (const QString &base : current) {
                    if (!FileCpp::exists(base.toStdString()) || !FileCpp::isDir(base.toStdString()) || FileCpp::isSymLink(base.toStdString())) {
                        continue;
                    }
                    DirCpp::Filter f = DirCpp::Filter::NoDotAndDotDot;
                    if (isLast) {
                        f = static_cast<DirCpp::Filter>(static_cast<unsigned>(f) | static_cast<unsigned>(DirCpp::Filter::AllEntries));
                    } else {
                        f = static_cast<DirCpp::Filter>(static_cast<unsigned>(f) | static_cast<unsigned>(DirCpp::Filter::Dirs)
                                                       | static_cast<unsigned>(DirCpp::Filter::NoSymLinks));
                    }
                    const std::vector<DirCpp::FileInfo> entries = DirCpp(base.toStdString()).entryInfoList(f);
                    for (const DirCpp::FileInfo &entry : entries) {
                        const QString fileName = QString::fromLocal8Bit(entry.fileName.c_str());
                        if (!glob_qt_default_match_component(component, fileName)) {
                            continue;
                        }
                        if (!isLast && entry.isSymLink) {
                            continue;
                        }
                        next.append(QString::fromLocal8Bit(entry.filePath.c_str()));
                    }
                }
            } else {
                for (const QString &base : current) {
                    const QString candidate = QString::fromLocal8Bit(DirCpp(base.toStdString()).filePath(component.toStdString()).c_str());
                    if (!FileCpp::exists(candidate.toStdString()) && !FileCpp::isSymLink(candidate.toStdString())) {
                        continue;
                    }
                    if (!isLast) {
                        if (!FileCpp::isDir(candidate.toStdString()) || FileCpp::isSymLink(candidate.toStdString())) {
                            continue;
                        }
                    }
                    next.append(candidate);
                }
            }
            current = next;
            if (current.isEmpty()) {
                break;
            }
        }
        return current;
    };
    QStringList expandedExcludes;
    expandedExcludes.reserve(excludes.size());
    for (const QString &rawValue : excludes) {
        QString cleaned = rawValue;
        const int bangIndex = cleaned.indexOf('!');
        if (bangIndex != -1) { // Truncate things like "!(minstall.desktop)"
            cleaned.truncate(bangIndex);
        }
        if (cleaned.isEmpty()) {
            continue;
        }
        const QStringList matches = expandExcludePattern(cleaned);
        for (const QString &match : matches) {
            if (!isAllowedDevice(match)) {
                continue;
            }
            const QString normalized = normalizeExclude(match);
            if (!normalized.isEmpty()) {
                expandedExcludes.append(normalized);
            }
        }
    }
    excludes = expandedExcludes;

    // Filter out nested paths to avoid double-counting in size calculation
    std::sort(excludes.begin(), excludes.end(), [](const QString &a, const QString &b) {
        return a.length() < b.length();
    });

    QStringList filteredExcludes;
    for (const QString &path : excludes) {
        bool isNested = false;
        for (const QString &accepted : filteredExcludes) {
            if (accepted == "/") {
                isNested = path != "/";
                break;
            }
            if (path == accepted || path.startsWith(accepted + '/')) {
                isNested = true;
                break;
            }
        }
        if (!isNested) {
            filteredExcludes.append(path);
        }
    }
    excludes = filteredExcludes;

    notifyMessage(QObject::tr("Calculating total size of excluded files..."));
    bool ok = true;
    quint64 excl_size = 0;
    if (!excludes.isEmpty()) {
        TempFileCpp excludeList;
        excludeList.setAutoRemove(true);
        if (!excludeList.open()) {
            ok = false;
        } else {
            for (const QString &path : excludes) {
                const QByteArray b = path.toLocal8Bit();
                if (!excludeList.writeAll(b.constData(), static_cast<std::size_t>(b.size()))) {
                    ok = false;
                    break;
                }
                const char z = '\0';
                if (!excludeList.writeAll(&z, 1)) {
                    ok = false;
                    break;
                }
            }
            if (ok) {
                ok = excludeList.flush();
            }
            QStringList duArgs = settings->live ? QStringList {"-sc", "-P", "--apparent-size"}
                                                : QStringList {"-sxc", "-P"};
            if (ok) {
                duArgs << "--files0-from=" + QString::fromLocal8Bit(excludeList.fileName().c_str());
                excl_size = static_cast<quint64>(
                    WorkCppUtils::parseDuKilobytes(getOutAsRoot("du", duArgs, CommandRunner::QuietMode::Yes), &ok));
            }
            excludeList.close();
        }
    }
    if (!ok) {
        qDebug() << "Error: calculating size of excluded files\n"
                    "If you are sure you have enough free space rerun the program with -o/--override-size option";
        cleanUp();
    }
    notifyMessage(QObject::tr("Calculating size of root..."));
    quint64 root_size = 0;
    if (settings->live) {
        root_size = static_cast<quint64>(WorkCppUtils::parseDuKilobytes(
            getOutAsRoot("du", {"-s", "-P", "--apparent-size", sizeRoot}, CommandRunner::QuietMode::Yes), &ok));
    } else {
        root_size = static_cast<quint64>(WorkCppUtils::parseDuKilobytes(
            getOutAsRoot("du", {"-sx", "-P", sizeRoot}, CommandRunner::QuietMode::Yes), &ok));
    }
    if (!settings->live && includeHomeDevice) {
        root_size += static_cast<quint64>(WorkCppUtils::parseDuKilobytes(
            getOutAsRoot("du", {"-sx", "-P", "/home"}, CommandRunner::QuietMode::Yes), &ok));
    }
    constexpr double kibToMib = 1024.0;
    if (!ok) {
        qDebug() << "Error: calculating root size.\n"
                    "If you are sure you have enough free space rerun the program with -o/--override-size option";
        cleanUp();
    }
    if (excl_size > root_size) {
        qWarning() << "Excluded size exceeds root size; clamping excluded size for estimate."
                   << "Root:" << root_size << "Excluded:" << excl_size;
        excl_size = root_size;
    }
    qDebug().noquote() << "SIZE         " << QString::number(root_size / kibToMib, 'f', 2) << "MiB";
    qDebug().noquote() << "SIZE EXCLUDES" << QString::number(excl_size / kibToMib, 'f', 2) << "MiB";
    const uint c_factor = settings->compressionFactorValue(settings->compression);
    qDebug() << "COMPRESSION  " << c_factor;
    qDebug().noquote() << "SIZE NEEDED  "
                       << QString::number(((root_size - excl_size) * c_factor / 100.0) / kibToMib, 'f', 2) << "MiB";
    qDebug().noquote() << "SIZE FREE    " << QString::number(settings->freeSpace / kibToMib, 'f', 2) << "MiB" << '\n';

    return (root_size - excl_size) * c_factor / 100;
}
