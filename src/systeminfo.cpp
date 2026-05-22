#include "systeminfo.h"

#include <cstring>
#include <cstdio>
#include <string>
#include <unistd.h>

#include "process_runner.h"
#include "command_runner.h"

static std::string basename_qt_fileinfo_basename_semantics(const std::string &path)
{
    const char *s = path.c_str();
    const char *slash = std::strrchr(s, '/');
    std::string file = slash ? std::string(slash + 1) : path;
    const std::size_t dot = file.rfind('.');
    if (dot != std::string::npos && dot != 0) {
        file.resize(dot);
    }
    return file;
}

static std::string proc_self_cmdline_argv0()
{
    FILE *f = std::fopen("/proc/self/cmdline", "rb");
    if (!f) {
        return std::string();
    }
    std::string buf;
    buf.resize(4096);
    const std::size_t n = std::fread(buf.data(), 1, buf.size(), f);
    std::fclose(f);
    if (n == 0) {
        return std::string();
    }
    const std::size_t end0 = buf.find('\0');
    if (end0 == std::string::npos) {
        buf.resize(n);
        return buf;
    }
    buf.resize(end0);
    return buf;
}

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
    const std::string argv0 = proc_self_cmdline_argv0();
    const std::string appName = basename_qt_fileinfo_basename_semantics(argv0);
    const std::string cmd = std::string("/usr/share/") + appName + "/scripts/snapshot-bootparameter.sh | tr '\n' ' '";
    return QString::fromStdString(CommandRunner::getOut(cmd));
}

