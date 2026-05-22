#include "systeminfo_cpp.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

#include "command_runner.h"
#include "process_runner.h"

namespace {

#ifdef UNIT_TESTS
static const SystemInfoCpp::Hooks *g_hooks = nullptr;
#endif

std::string basename_qt_fileinfo_basename_semantics(const std::string &path)
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

std::string proc_self_cmdline_argv0()
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

std::vector<std::string> split_newline_keep_empty_parts(const std::string &s)
{
    std::vector<std::string> out;

    std::size_t start = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\n') {
            out.emplace_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    out.emplace_back(s.substr(start));
    return out;
}
} // namespace

#ifdef UNIT_TESTS
void SystemInfoCpp::setHooksForTests(const Hooks *hooks)
{
    g_hooks = hooks;
}
#endif

bool SystemInfoCpp::is386()
{
    return sizeof(void *) == 4;
}

bool SystemInfoCpp::isLive()
{
#ifdef UNIT_TESTS
    if (g_hooks && g_hooks->isLive) {
        return g_hooks->isLive();
    }
#endif
    return ProcessRunner::execute("mountpoint", { "-q", "/live/aufs" }) == 0;
}

std::vector<std::string> SystemInfoCpp::listUsers()
{
    const std::string out = CommandRunner::getOut(
        "lslogins --noheadings -u -o user |grep -vw root", CommandRunner::QuietMode::Yes);
    return split_newline_keep_empty_parts(out);
}

std::string SystemInfoCpp::readKernelOpts()
{
    const std::string argv0 = proc_self_cmdline_argv0();
    const std::string appName = basename_qt_fileinfo_basename_semantics(argv0);
    const std::string cmd
        = std::string("/usr/share/") + appName + "/scripts/snapshot-bootparameter.sh | tr '\n' ' '";
    return CommandRunner::getOut(cmd);
}
