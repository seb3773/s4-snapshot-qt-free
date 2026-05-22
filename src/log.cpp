/**********************************************************************
 * Log.cpp
 **********************************************************************
 * Copyright (C) 2023-2025 MX Authors
 *
 * Authors: Adrian <adrian@local>
 *          Debian <http://debian.org>
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this package. If not, see <http://www.gnu.org/licenses/>.
 **********************************************************************/
#include "log.h"

#include <unistd.h>
#include <sys/stat.h>

#include "command_runner.h"
#include "datetime_cpp.h"
#include "logger_cpp.h"
#include "stdio_cpp.h"

#ifdef CLI_BUILD

Log::Log(const std::string &fileName)
{
    logFile.setFileName(fileName);

    if (FileCpp::exists(fileName)) {
        fixLogFileOwnership(fileName);
    }

    if (!logFile.open(FileCpp::OpenMode::WriteOnly | FileCpp::OpenMode::Append | FileCpp::OpenMode::Text)) {
        LoggerCpp::log(LoggerCpp::Level::Warning, std::string("Could not open log file: ") + fileName);
    }
}

void Log::writeMessage(LoggerCpp::Level level, const std::string &utf8Message)
{
    LoggerCpp::log(level, utf8Message);

    if (!logFile.isOpen()) {
        return;
    }

    const std::string line = LoggerCpp::formatFileLine(level, utf8Message);
    (void)logFile.write(line);
    (void)logFile.flush();
}

std::string Log::getLog()
{
    return logFile.fileName();
}

void Log::fixLogFileOwnership(const std::string &fileName)
{
    if (!FileCpp::exists(fileName)) {
        return;
    }

    const uid_t currentUid = getuid();

    struct stat fileStat;
    if (stat(fileName.c_str(), &fileStat) != 0) {
        return;
    }

    if (fileStat.st_uid == 0 && currentUid != 0) {
        const std::string username = CommandRunner::loggedInUserName();
        if (!username.empty()) {
            const CommandRunner::Result r = CommandRunner::procAsRoot(
                "chown", { username + ":", fileName }, std::string(), CommandRunner::QuietMode::Yes);
            if (r.started && r.normalExit && r.exitCode == 0) {
                LoggerCpp::log(LoggerCpp::Level::Debug, std::string("Fixed log file ownership for: ") + fileName);
            }
        }
    } else if (fileStat.st_uid != 0 && currentUid == 0) {
        if (chown(fileName.c_str(), 0, 0) == 0) {
            const std::string msg = std::string("Took ownership of log file as root: ") + fileName;
            LoggerCpp::log(LoggerCpp::Level::Debug, msg);
            (void)StdioCpp::write(stdout, msg + "\n");
            chmod(fileName.c_str(), S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        }
    }
}

#else

Log::Log(const QString &fileName)
{
    logFile.setFileName(fileName.toStdString());

    if (FileCpp::exists(fileName.toStdString())) {
        fixLogFileOwnership(fileName);
    }

    if (!logFile.open(FileCpp::OpenMode::WriteOnly | FileCpp::OpenMode::Append | FileCpp::OpenMode::Text)) {
        qWarning() << "Could not open log file:" << fileName;
    } else {
        qInstallMessageHandler(Log::messageHandler);
    }
}

void Log::messageHandler(QtMsgType type, [[maybe_unused]] const QMessageLogContext &context, const QString &msg)
{
    if (msg.contains('\r') || msg.startsWith("\033[2K")) {
        const QByteArray termOutBytes = (QStringLiteral("\033[?25l") + msg).toUtf8();
        (void)StdioCpp::write(stdout, std::string(termOutBytes.constData(), static_cast<size_t>(termOutBytes.size())));
        return;
    }

    const QByteArray termOutBytes = (msg + QLatin1Char('\n')).toUtf8();
    (void)StdioCpp::write(stdout, std::string(termOutBytes.constData(), static_cast<size_t>(termOutBytes.size())));

    if (!logFile.isOpen()) {
        qWarning() << "Log file is not open for writing:" << QString::fromLocal8Bit(logFile.fileName().c_str());
        // Try to fix ownership and reopen
        fixLogFileOwnership(QString::fromLocal8Bit(logFile.fileName().c_str()));
        if (!logFile.open(FileCpp::OpenMode::WriteOnly | FileCpp::OpenMode::Append | FileCpp::OpenMode::Text)) {
            qWarning() << "Still could not open log file after ownership fix";
            return;
        }
    }

    QString line = QString::fromStdString(DateTimeCpp::nowLocalYmdHmsMillis());

    switch (type) {
    case QtInfoMsg:
        line += "INF";
        break;
    case QtDebugMsg:
        line += "DBG";
        break;
    case QtWarningMsg:
        line += "WRN";
        break;
    case QtCriticalMsg:
        line += "CRT";
        break;
    case QtFatalMsg:
        line += "FTL";
        break;
    }

    line += ": ";
    line += msg;
    line += '\n';

    const std::string outBytes = line.toLocal8Bit().toStdString();
    (void)logFile.write(outBytes);
    (void)logFile.flush();
}

QString Log::getLog()
{
    return QString::fromLocal8Bit(logFile.fileName().c_str());
}

void Log::fixLogFileOwnership(const QString &fileName)
{
    if (!FileCpp::exists(fileName.toStdString())) {
        return;
    }

    // Get current user information
    const uid_t currentUid = getuid();

    // Get file ownership and permissions
    struct stat fileStat;
    const QByteArray fileNameBytes = fileName.toLocal8Bit();
    if (stat(fileNameBytes.constData(), &fileStat) != 0) {
        return;
    }

    // Case 1: Running as regular user, but file is owned by root
    if (fileStat.st_uid == 0 && currentUid != 0) {
        const QString username = QString::fromStdString(CommandRunner::loggedInUserName());
        if (!username.isEmpty()) {
            const CommandRunner::Result r = CommandRunner::procAsRoot(
                "chown", { (username + ":").toStdString(), fileName.toStdString() }, std::string(),
                CommandRunner::QuietMode::Yes);
            if (r.started && r.normalExit && r.exitCode == 0) {
            qDebug() << "Fixed log file ownership for:" << fileName;
            }
        }
    }
    // Case 2: Running as root, but file is owned by regular user with restrictive permissions
    else if (fileStat.st_uid != 0 && currentUid == 0) {
        // When running as root, take ownership of the log file for consistency
        if (chown(fileNameBytes.constData(), 0, 0) == 0) {
            qDebug() << "Took ownership of log file as root:" << fileName;
            // Also ensure it has reasonable permissions
            chmod(fileNameBytes.constData(), S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        }
    }
}

#endif
