/**********************************************************************
 *
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
#pragma once

#include "file_cpp.h"

#ifdef CLI_BUILD

#include "logger_cpp.h"

#include <string>

class Log
{
public:
    explicit Log(const std::string &fileName = "/tmp/mxpi.log");
    static std::string getLog();
    static void writeMessage(LoggerCpp::Level level, const std::string &utf8Message);

private:
    inline static FileCpp logFile;
    static void fixLogFileOwnership(const std::string &fileName);
};

#else

#include <QDebug>
#include <QString>

class Log
{
public:
    explicit Log(const QString &fileName = "/tmp/mxpi.log");
    static QString getLog();
    static void messageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg);

private:
    inline static FileCpp logFile;
    static void fixLogFileOwnership(const QString &fileName);
};

#endif
