#include "command_runner.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include <unistd.h>

#include "embedded/embedded_helper_runtime.h"
#include "process_runner.h"

namespace {

static const CommandRunner::Hooks *g_hooks = nullptr;
static const CommandRunner::GuiElevationHooks *g_gui_elevation_hooks = nullptr;

static bool sudo_auth_failed(const std::string &text)
{
    return text.find("incorrect password") != std::string::npos
        || text.find("Sorry, try again") != std::string::npos
        || text.find("authentication failure") != std::string::npos
        || text.find("3 incorrect password attempts") != std::string::npos;
}

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

#ifndef CLI_BUILD
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
#endif

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

static CommandRunner::Result result_from_process(const ProcessRunner::Result &r)
{
    CommandRunner::Result out;
    out.started = r.started;
    out.exitCode = r.exitCode;
    out.normalExit = (r.exitStatus == ProcessRunner::ExitStatus::NormalExit);
    out.stdoutText = r.stdoutText;
    out.stderrText = r.stderrText;
    out.mergedText = merge_for_display(out.stdoutText, out.stderrText);
    return out;
}

} // namespace

void CommandRunner::setHooksForTests(const Hooks *hooks)
{
    g_hooks = hooks;
}

void CommandRunner::setGuiElevationHooks(const GuiElevationHooks *hooks)
{
    g_gui_elevation_hooks = hooks;
}

void CommandRunner::clearGuiElevationHooks()
{
    g_gui_elevation_hooks = nullptr;
}

std::string CommandRunner::elevationTool()
{
    if (g_hooks && g_hooks->elevationTool) {
        return g_hooks->elevationTool();
    }
    const bool cli = CommandRunner::isCliMode();
    if (cli) {
        if (file_exists("/usr/bin/sudo")) return std::string("/usr/bin/sudo");
        if (file_exists("/usr/bin/doas")) return std::string("/usr/bin/doas");
        if (file_exists("/usr/bin/gksu")) return std::string("/usr/bin/gksu");
        return {};
    }
    if (file_exists("/usr/bin/sudo")) return std::string("/usr/bin/sudo");
    if (file_exists("/usr/bin/doas")) return std::string("/usr/bin/doas");
    if (file_exists("/usr/bin/gksu")) return std::string("/usr/bin/gksu");
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

    const EmbeddedHelperRuntime::Result helper = EmbeddedHelperRuntime::ensureHelperAvailable();
    if (!helper.ok) {
        out.started = false;
        out.exitCode = 1;
        out.normalExit = true;
        out.stderrText = helper.error + "\n";
        out.mergedText = out.stderrText;
        return out;
    }
    const std::string helperPath = helper.path;

    if (::getuid() == 0) {
        const ProcessRunner::Result r = ProcessRunner::run(helperPath, helperArgs, stdinText, -1);
        return result_from_process(r);
    }

    const std::string elevationToolPath = CommandRunner::elevationTool();
    if (elevationToolPath.empty()) {
        out.started = false;
        out.exitCode = 1;
        out.normalExit = true;
        out.stderrText = "No elevation tool found (sudo/doas/gksu).\n";
        out.mergedText = out.stderrText;
        return out;
    }

    const bool useGuiElevation = !CommandRunner::isCliMode()
        && g_gui_elevation_hooks != nullptr
        && g_gui_elevation_hooks->askPassword != nullptr
        && elevationToolPath.find("sudo") != std::string::npos;

    if (useGuiElevation) {
        std::vector<std::string> sudoArgs {"-S", "-p", "", helperPath};
        sudoArgs.insert(sudoArgs.end(), helperArgs.begin(), helperArgs.end());

        for (int attempt = 0; attempt < 2; ++attempt) {
            const std::string password = g_gui_elevation_hooks->askPassword();
            if (password.empty()) {
                out.started = false;
                out.exitCode = 1;
                out.normalExit = true;
                out.stderrText = "Administrator password required.\n";
                out.mergedText = out.stderrText;
                return out;
            }

            std::string sudoStdin = password;
            sudoStdin.push_back('\n');
            sudoStdin += stdinText;

            const ProcessRunner::Result r = ProcessRunner::run(elevationToolPath, sudoArgs, sudoStdin, -1);
            out = result_from_process(r);
            if (!sudo_auth_failed(out.mergedText)) {
                static constexpr int EXIT_CODE_COMMAND_NOT_FOUND = 127;
                static constexpr int EXIT_CODE_PERMISSION_DENIED = 126;
                if (out.exitCode == EXIT_CODE_COMMAND_NOT_FOUND || out.exitCode == EXIT_CODE_PERMISSION_DENIED) {
                    handleElevationError();
                }
                return out;
            }

            if (g_gui_elevation_hooks->invalidateCachedPassword) {
                g_gui_elevation_hooks->invalidateCachedPassword();
            }
        }

        out.started = false;
        out.exitCode = 1;
        out.normalExit = true;
        out.stderrText = "Incorrect administrator password.\n";
        out.mergedText = out.stderrText;
        return out;
    }

    std::vector<std::string> programArgs = helperArgs;
    programArgs.insert(programArgs.begin(), helperPath);

    const ProcessRunner::Result r = ProcessRunner::run(elevationToolPath, programArgs, stdinText, -1);
    out = result_from_process(r);

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
#ifndef CLI_BUILD
    if (!isCliMode()) {
        return;
    }
#endif
    std::exit(EXIT_FAILURE);
}
