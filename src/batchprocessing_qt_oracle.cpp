/**********************************************************************
 *  batchprocessing.cpp
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

#include "batchprocessing_qt_oracle.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <chrono>
#include <utime.h>

#include "process_runner.h"
#include "command_runner.h"
#include "file_cpp.h"
#include "dir_cpp.h"
#include "stdio_cpp.h"
#include "work.h"

using namespace std::chrono_literals;

BatchprocessingQtOracle::BatchprocessingQtOracle(Settings *settings, QObject *parent)
    : QObject(parent),
      settings(settings),
      work(settings)
{
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] { work.cleanUp(); });
    setConnections();

    // Check updated excludes before any work
    checkUpdatedDefaultExcludesCli();

    if (!settings->checkCompression()) {
        qCritical().noquote() << tr("Error")
                              << tr("Current kernel doesn't support selected compression algorithm, "
                                    "please edit the configuration file and select a different algorithm.");
        return;
    }

    QString path = settings->snapshotDir;
    if (path.endsWith(QStringLiteral("/snapshot"))) {
        path.chop(static_cast<int>(QStringLiteral("/snapshot").size()));
    } else if (path.endsWith(QStringLiteral("/snapshot\r\n"))) {
        path.chop(static_cast<int>(QStringLiteral("/snapshot\r\n").size()));
        path += QStringLiteral("\r\n");
    } else if (path.endsWith(QStringLiteral("/snapshot\n"))) {
        path.chop(static_cast<int>(QStringLiteral("/snapshot\n").size()));
        path += QStringLiteral("\n");
    }
    qDebug() << "Free space:" << settings->getFreeSpaceStrings(path);
    if (!settings->monthly && !settings->overrideSize) {
        qDebug() << "Unused space:" << settings->getUsedSpace();
    }

    work.startTimer();
    if (!settings->checkSnapshotDir() || !settings->checkTempDir()) {
        work.cleanUp();
        return;
    }
    settings->otherExclusions();
    work.setupEnv();
    if (!settings->monthly && !settings->overrideSize) {
        work.checkEnoughSpace();
    }
    work.copyNewIso();
    work.savePackageList(settings->snapshotName);

    if (settings->editBootMenu) {
        qDebug() << tr("The program will pause the build and open the boot menu in your text editor.");
        QString cmd = settings->getEditor() + " \"" + settings->workDir + "/iso-template/boot/grub/grub.cfg\" \""
                      + settings->workDir + "/iso-template/boot/syslinux/syslinux.cfg\" \"" + settings->workDir
                      + "/iso-template/boot/isolinux/isolinux.cfg\"";
        (void)CommandRunner::run(cmd.toStdString());
    }
    disconnect(&timer, &QTimer::timeout, nullptr, nullptr);
    work.createIso(settings->snapshotName);
}

void BatchprocessingQtOracle::setConnections()
{
    connect(&timer, &QTimer::timeout, this, &BatchprocessingQtOracle::progress);
    work.setMessageCallback([](const QString &out) { qDebug().noquote() << out; });
    work.setMessageBoxCallback(
        [](BoxType /*unused*/, const QString &title, const QString &msg) { qDebug().noquote() << title << msg; });
}

void BatchprocessingQtOracle::progress()
{
    static bool toggle = false;
    qDebug() << "\033[2KProcessing command" << (toggle ? "...\r" : "\r");
    toggle = !toggle;
}

bool BatchprocessingQtOracle::isSourceExcludesNewer(QString &diffOutput) const
{
    const QString configuredPath = settings->snapshotExcludesPath;
    const QString sourcePath = settings->getExcludesSourcePath();

    qDebug().noquote() << "CLI excludes check:"
                       << "configured=" << configuredPath
                       << "source=" << sourcePath;

    if (sourcePath.isEmpty() || configuredPath.isEmpty()) {
        qDebug() << "CLI excludes check: empty path(s)";
        return false;
    }

    const bool configuredExists = FileCpp::exists(configuredPath.toStdString());
    const bool sourceExists = FileCpp::exists(sourcePath.toStdString());
    if (!configuredExists || !sourceExists) {
        qDebug() << "CLI excludes check: missing files"
                 << configuredExists << sourceExists;
        return false;
    }

    const std::int64_t configuredMtime = FileCpp::lastModifiedSecsSinceEpoch(configuredPath.toStdString());
    const std::int64_t sourceMtime = FileCpp::lastModifiedSecsSinceEpoch(sourcePath.toStdString());

    qDebug() << "CLI excludes check: mtime configured"
             << QDateTime::fromSecsSinceEpoch(configuredMtime)
             << "source"
             << QDateTime::fromSecsSinceEpoch(sourceMtime);

    if (QDateTime::fromSecsSinceEpoch(sourceMtime) <= QDateTime::fromSecsSinceEpoch(configuredMtime)) {
        qDebug() << "CLI excludes check: source not newer";
        return false;
    }

    const ProcessRunner::Result r = ProcessRunner::run(
        "diff",
        {"--unified", configuredPath.toStdString(), sourcePath.toStdString()},
        std::string(),
        30000);
    if (!r.started) {
        qWarning() << "Unable to compare excludes files with diff:" << configuredPath << sourcePath;
        return false;
    }

    const int diffResult = r.exitCode;
    qDebug() << "CLI excludes check: diff exit code" << diffResult;
    if (diffResult == 0) {
        return false;
    }
    if (diffResult != 1) {
        qWarning() << "Unable to compare excludes files with diff:" << configuredPath << sourcePath;
        return false;
    }

    diffOutput = QString::fromStdString(r.stdoutText);
    if (diffOutput.isEmpty()) {
        diffOutput = QString::fromStdString(r.stderrText);
    }
    if (diffOutput.isEmpty()) {
        diffOutput = tr("No diff output available.");
    }
    return true;
}

QString BatchprocessingQtOracle::colorizeDiffAnsi(const QString &diff)
{
    static const QString green = "\033[32m";
    static const QString red = "\033[31m";
    static const QString blue = "\033[34m";
    static const QString gray = "\033[90m";
    static const QString reset = "\033[0m";

    QStringList lines = diff.split('\n');
    QString colored;
    for (const QString &line : lines) {
        if (line.startsWith("+++ ") || line.startsWith("--- ")) {
            colored += gray + line + reset + '\n';
        } else if (line.startsWith("@@")) {
            colored += blue + line + reset + '\n';
        } else if (line.startsWith('+')) {
            colored += green + line + reset + '\n';
        } else if (line.startsWith('-')) {
            colored += red + line + reset + '\n';
        } else {
            colored += line + '\n';
        }
    }
    return colored;
}

bool BatchprocessingQtOracle::resetCustomExcludesCli(const QString &configuredPath, const QString &sourcePath) const
{
    if (sourcePath.isEmpty() || configuredPath.isEmpty()) {
        return false;
    }

    if (!FileCpp::exists(sourcePath.toStdString())) {
        qWarning().noquote() << tr("Default exclusion file not found at %1.").arg(sourcePath);
        return false;
    }

    const QString targetDir = QString::fromStdString(DirCpp::absolutePathOfContainingDir(configuredPath.toStdString()));
    if (!targetDir.isEmpty()) {
        (void)DirCpp().mkpath(targetDir.toStdString());
    }

    if (FileCpp::exists(configuredPath.toStdString())) {
        const QString backupPath = configuredPath + "." + QDateTime::currentDateTime().toString("yyyyMMddhhmmss");
        if (!FileCpp::copy(configuredPath.toStdString(), backupPath.toStdString())) {
            qWarning().noquote() << tr("Could not backup existing exclusion file to %1.").arg(backupPath);
        }
        if (!FileCpp::remove(configuredPath.toStdString())) {
            qWarning().noquote() << tr("Could not remove existing exclusion file at %1.").arg(configuredPath);
            return false;
        }
    }

    if (!FileCpp::copy(sourcePath.toStdString(), configuredPath.toStdString())) {
        qWarning().noquote()
            << tr("Could not copy default exclusion file from %1 to %2.").arg(sourcePath, configuredPath);
        return false;
    }

    return true;
}

void BatchprocessingQtOracle::checkUpdatedDefaultExcludesCli()
{
    QString diffOutput;
    if (!isSourceExcludesNewer(diffOutput)) {
        return;
    }

    const QString configuredPath = settings->snapshotExcludesPath;
    const QString sourcePath = settings->getExcludesSourcePath();
    qDebug().noquote() << tr("Detected newer exclusion file at %1 compared to %2. Prompting for action.")
                              .arg(sourcePath, configuredPath);

    const QString showOptionKey = tr("s", "CLI excludes prompt: single-letter shortcut for 'show diff'");
    const QString useOptionKey = tr("u", "CLI excludes prompt: single-letter shortcut for 'use updated default'");
    const QString keepOptionKey = tr("k", "CLI excludes prompt: single-letter shortcut for 'keep custom (update timestamp)'");
    const QString quitOptionKey = tr("q", "CLI excludes prompt: single-letter shortcut for 'quit'");

    const QString showOptionText = tr("show diff", "CLI excludes prompt option label");
    const QString useOptionText = tr("use updated default", "CLI excludes prompt option label");
    const QString keepOptionText = tr("keep custom (update timestamp)", "CLI excludes prompt option label");
    const QString quitOptionText = tr("quit", "CLI excludes prompt option label");

    const QString optionPrompt =
        tr("[%1]%2  [%3]%4  [%5]%6  [%7]%8: ")
            .arg(showOptionKey, showOptionText, useOptionKey, useOptionText, keepOptionKey, keepOptionText, quitOptionKey,
                 quitOptionText);

    while (true) {
        (void)StdioCpp::write(stdout,
                        tr("The exclusion file at %1 is newer than your configured file at %2.")
                            .arg(sourcePath, configuredPath)
                            .toLocal8Bit()
                            .toStdString());
        (void)StdioCpp::write(stdout, "\n");
        (void)StdioCpp::write(stdout, optionPrompt.toLocal8Bit().toStdString());
        (void)StdioCpp::flush(stdout);

        const QString response = QString::fromLocal8Bit(StdioCpp::readLine(stdin).c_str()).trimmed();

        if (response.compare(showOptionKey, Qt::CaseInsensitive) == 0
            || response.compare(showOptionText, Qt::CaseInsensitive) == 0) {
            (void)StdioCpp::write(stdout, colorizeDiffAnsi(diffOutput).toLocal8Bit().toStdString());
            (void)StdioCpp::flush(stdout);
            continue;
        }

        if (response.compare(useOptionKey, Qt::CaseInsensitive) == 0
            || response.compare(useOptionText, Qt::CaseInsensitive) == 0) {
            if (resetCustomExcludesCli(configuredPath, sourcePath)) {
                qDebug().noquote() << tr("Reverted to updated default exclusion file.");
            }
            return;
        }

        if (response.compare(keepOptionKey, Qt::CaseInsensitive) == 0
            || response.compare(keepOptionText, Qt::CaseInsensitive) == 0) {
            utimbuf times {};
            times.actime = FileCpp::lastReadSecsSinceEpoch(configuredPath.toStdString());
            times.modtime = QDateTime::currentSecsSinceEpoch();
            const int utimeResult = utime(configuredPath.toLocal8Bit().constData(), &times);
            if (utimeResult == 0) {
                qDebug() << "Updated modification time for custom excludes file via utime" << configuredPath;
            } else {
                qWarning() << "Failed to update modification time for custom excludes file" << configuredPath;
            }
            return;
        }

        if (response.compare(quitOptionKey, Qt::CaseInsensitive) == 0
            || response.compare(quitOptionText, Qt::CaseInsensitive) == 0 || response.isEmpty()) {
            qDebug() << tr("Leaving custom exclusion file unchanged.");
            const bool debugStop = qEnvironmentVariableIsSet("MX_SNAPSHOT_EXCLUDES_DEBUG_STOP");
            if (debugStop) {
                qDebug() << "Debug stop requested; exiting after excludes check.";
                QCoreApplication::exit(0);
            } else {
                QCoreApplication::exit(EXIT_SUCCESS);
            }
            return; // exit requested
        }

        (void)StdioCpp::write(stdout, tr("Invalid choice. Please select again.").toLocal8Bit().toStdString());
        (void)StdioCpp::write(stdout, "\n");
    }
}

void BatchprocessingQtOracle::checkNvidiaGraphicsCard()
{
    if (CommandRunner::run("glxinfo | grep -q NVIDIA")) {
        qDebug() << tr("This computer uses an NVIDIA graphics card. Are you planning to use the "
                       "resulting ISO on the same computer or another computer with an NVIDIA card?")
                + " yes/no";
        QString response = QString::fromLocal8Bit(StdioCpp::readWord(stdin).c_str());

        response = response.toLower();
        if (response == "yes" || response == "y") {
            settings->bootOptions += " xorg=nvidia";
            qDebug() << tr("Note: If you use the resulting ISO on a computer without an NVIDIA card, "
                           "you will likely need to remove 'xorg=nvidia' from the boot options.");
        } else {
            qDebug() << tr("Note: If you use the resulting ISO on a computer with an NVIDIA card, "
                           "you may need to add 'xorg=nvidia' to the boot options.");
        }
    }
}
