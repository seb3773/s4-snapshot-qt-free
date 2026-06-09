#include "systeminfo.h"

#include "process_runner.h"
#include "command_runner.h"
#include "systeminfo_cpp.h"

bool SystemInfo::is386()
{
    return sizeof(void *) == 4;
}

bool SystemInfo::isLive()
{
    return ProcessRunner::execute("mountpoint", {"-q", "/live/aufs"}) == 0;
}

QStringList SystemInfo::listUsers()
{
    const QString out = QString::fromStdString(CommandRunner::getOut(
        "lslogins --noheadings -u -o user |grep -vw root", CommandRunner::QuietMode::Yes));
    QStringList list;
    qsizetype start = 0;
    for (qsizetype i = 0; i < out.size(); ++i) {
        if (out.at(i) == QLatin1Char('\n')) {
            list.append(out.mid(start, i - start));
            start = i + 1;
        }
    }
    list.append(out.mid(start));
    return list;
}

QString SystemInfo::readKernelOpts()
{
    return QString::fromStdString(SystemInfoCpp::readKernelOpts());
}

