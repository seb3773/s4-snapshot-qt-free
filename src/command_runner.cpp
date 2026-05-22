#include "command_runner.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include <unistd.h>

#include "process_runner.h"

namespace {

static const CommandRunner::Hooks *g_hooks = nullptr;

static std::string trim_copy(const std::string &s)
{
    size_t b = 0;
    while (b < s.size() && (s[b] == ' ' || s[b] == '\n' || s[b] == '\r' || s[b] == '\t')) {
        ++b;
    }
    size_t e = s.size();
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\n' || s[e - 1] == '\r' || s[e - 1] == '\t')) {
        --e;
    }
    return s.substr(b, e - b);
}

static bool file_exists(const char *path)
{
    return ::access(path, F_OK) == 0;
}

static std::string readlink_string(const char *path)
{
    std::string out;
    out.resize(4096);
    const ssize_t n = ::readlink(path, out.data(), out.size() - 1);
    if (n <= 0) {
        return {};
    }
    out.resize(static_cast<size_t>(n));
    return out;
}

static std::string getenv_string(const char *name)
{
    const char *v = ::getenv(name);
    return v ? std::string(v) : std::string();
}

static std::string merge_for_display(const std::string &stdoutText, const std::string &stderrText)
{
    std::string merged;
    merged.reserve(stdoutText.size() + stderrText.size());
    merged.append(stdoutText);
    merged.append(stderrText);
    return merged;
}

} // namespace

void CommandRunner::setHooksForTests(const Hooks *hooks)
{
    g_hooks = hooks;
}

std::string CommandRunner::elevationTool()
{
    if (g_hooks && g_hooks->elevationTool) {
        return g_hooks->elevationTool();
    }
    const bool cli = CommandRunner::isCliMode();
    if (cli) {
        if (file_exists("/usr/bin/sudo")) return std::string("/usr/bin/sudo");
        if (file_exists("/usr/bin/gksu")) return std::string("/usr/bin/gksu");
        if (file_exists("/usr/bin/pkexec")) return std::string("/usr/bin/pkexec");
        return {};
    }
    if (file_exists("/usr/bin/pkexec")) return std::string("/usr/bin/pkexec");
    if (file_exists("/usr/bin/gksu")) return std::string("/usr/bin/gksu");
    if (file_exists("/usr/bin/sudo")) return std::string("/usr/bin/sudo");
    return {};
}

bool CommandRunner::isCliMode()
{
#ifdef CLI_BUILD
    return true;
#else
    const std::string exePath = readlink_string("/proc/self/exe");
    bool forceCliMode = false;
    if (!exePath.empty() && exePath.find("cli") != std::string::npos) {
        forceCliMode = true;
    }
    const std::string mxCli = getenv_string("MX_SNAPSHOT_CLI");
    if (!mxCli.empty()) {
        forceCliMode = true;
    }

    const std::string display = getenv_string("DISPLAY");
    const std::string wayland = getenv_string("WAYLAND_DISPLAY");
    const bool noWindowSystem = display.empty() && wayland.empty();
    const std::string qpa = getenv_string("QT_QPA_PLATFORM");
    const bool headlessQpa = (qpa == "offscreen" || qpa == "minimal" || qpa == "linuxfb");

    return forceCliMode || noWindowSystem || headlessQpa;
#endif
}

std::string CommandRunner::loggedInUserName()
{
    if (g_hooks && g_hooks->loggedInUserName) {
        return g_hooks->loggedInUserName();
    }
    std::string username = getenv_string("SUDO_USER");
    if (username.empty()) {
        username = getenv_string("LOGNAME");
    }
    if (username.empty()) {
        username = getenv_string("USER");
    }
    if (username.empty() || username == "root") {
        username = trim_copy(CommandRunner::getOut("logname", QuietMode::Yes));
    }
    return username == "root" ? std::string() : username;
}

CommandRunner::Result CommandRunner::proc(const std::string &cmd, const std::vector<std::string> &args,
                                         const std::string &stdinText, QuietMode quiet, Elevation elevation)
{
    if (g_hooks && g_hooks->proc) {
        return g_hooks->proc(cmd, args, stdinText, quiet, elevation);
    }
    if (elevation == Elevation::Yes) {
        return helperProc(helperExecArgs(cmd, args), stdinText, quiet);
    }

    Result out;
    const ProcessRunner::Result r = ProcessRunner::runStreaming(
        cmd,
        args,
        stdinText,
        [&out](const char *data, size_t n) {
            out.stdoutText.append(data, n);
            out.mergedText.append(data, n);
        },
        [&out](const char *data, size_t n) {
            out.stderrText.append(data, n);
            out.mergedText.append(data, n);
        },
        -1);

    out.started = r.started;
    out.exitCode = r.exitCode;
    out.normalExit = (r.exitStatus == ProcessRunner::ExitStatus::NormalExit);

    if (out.mergedText.empty()) {
        out.mergedText = merge_for_display(out.stdoutText, out.stderrText);
    }

    return out;
}

CommandRunner::Result CommandRunner::procAsRoot(const std::string &cmd, const std::vector<std::string> &args,
                                               const std::string &stdinText, QuietMode quiet)
{
    return proc(cmd, args, stdinText, quiet, Elevation::Yes);
}

std::string CommandRunner::getOut(const std::string &cmd, QuietMode quiet)
{
    const Result r = proc("/bin/bash", {"-c", cmd}, std::string(), quiet, Elevation::No);
    return trim_copy(r.mergedText);
}

std::string CommandRunner::getOutAsRoot(const std::string &cmd, const std::vector<std::string> &args, QuietMode quiet)
{
    const Result r = procAsRoot(cmd, args, std::string(), quiet);
    return trim_copy(r.mergedText);
}

bool CommandRunner::run(const std::string &cmd, QuietMode quiet)
{
    const Result r = proc("/bin/bash", {"-c", cmd}, std::string(), quiet, Elevation::No);
    return r.normalExit && r.exitCode == 0;
}

CommandRunner::Result CommandRunner::helperProc(const std::vector<std::string> &helperArgs, const std::string &stdinText,
                                                QuietMode quiet)
{
    Result out;
    (void)quiet;

    const std::string elevationToolPath = CommandRunner::elevationTool();
    const std::string appName = []() {
        std::string exePath = readlink_string("/proc/self/exe");
        if (exePath.empty()) {
            return std::string("s4-snapshot");
        }
        const size_t pos = exePath.find_last_of('/');
        return (pos == std::string::npos) ? exePath : exePath.substr(pos + 1);
    }();

    const std::string helperPath = std::string("/usr/lib/") + appName + "/helper";

    if (::getuid() != 0 && elevationToolPath.empty()) {
        out.started = false;
        out.exitCode = 1;
        out.normalExit = true;
        out.stderrText = "No elevation tool found (pkexec/gksu/sudo).\n";
        out.mergedText = out.stderrText;
        return out;
    }

    const std::string program = (::getuid() == 0) ? helperPath : elevationToolPath;
    std::vector<std::string> programArgs = helperArgs;
    if (::getuid() != 0) {
        programArgs.insert(programArgs.begin(), helperPath);
    }

    const ProcessRunner::Result r = ProcessRunner::run(program, programArgs, stdinText, -1);
    out.started = r.started;
    out.exitCode = r.exitCode;
    out.normalExit = (r.exitStatus == ProcessRunner::ExitStatus::NormalExit);
    out.stdoutText = r.stdoutText;
    out.stderrText = r.stderrText;
    out.mergedText = merge_for_display(out.stdoutText, out.stderrText);

    static constexpr int EXIT_CODE_COMMAND_NOT_FOUND = 127;
    static constexpr int EXIT_CODE_PERMISSION_DENIED = 126;
    if (out.exitCode == EXIT_CODE_PERMISSION_DENIED || out.exitCode == EXIT_CODE_COMMAND_NOT_FOUND) {
        handleElevationError();
    }

    return out;
}

std::vector<std::string> CommandRunner::helperExecArgs(const std::string &cmd, const std::vector<std::string> &args)
{
    std::vector<std::string> helperArgs;
    helperArgs.reserve(args.size() + 2);
    helperArgs.push_back("exec");
    helperArgs.push_back(cmd);
    for (const std::string &a : args) {
        helperArgs.push_back(a);
    }
    return helperArgs;
}

void CommandRunner::handleElevationError()
{
    std::fprintf(stderr,
                 "This operation requires administrator privileges. Please restart the application and enter your password when prompted.\n");
    std::fflush(stderr);
    std::exit(EXIT_FAILURE);
}
