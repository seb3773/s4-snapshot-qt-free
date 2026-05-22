#include "cmd_cpp.h"

#include "command_runner.h"
#include "logger_cpp.h"

#ifdef CLI_BUILD
#include "messagehandler_cpp.h"
#endif

#include <cstdio>
#include <cstring>
#include <unistd.h>

namespace {
std::string proc_self_cmdline_argv0()
{
    std::FILE *f = std::fopen("/proc/self/cmdline", "rb");
    if (!f) {
        return std::string();
    }

    std::string out;
    char buf[256];
    while (true) {
        const std::size_t n = std::fread(buf, 1, sizeof(buf), f);
        if (n == 0) {
            break;
        }
        out.append(buf, buf + n);
        const std::size_t nul = out.find('\0');
        if (nul != std::string::npos) {
            out.resize(nul);
            break;
        }
    }

    std::fclose(f);
    return out;
}

std::string basename_qt_fileinfo_basename_semantics(const std::string &path)
{
    if (path.empty()) {
        return std::string();
    }

    std::size_t end = path.size();
    while (end > 0 && path[end - 1] == '/') {
        --end;
    }
    if (end == 0) {
        return std::string();
    }

    std::size_t slash = path.rfind('/', end - 1);
    const std::size_t start = (slash == std::string::npos) ? 0 : (slash + 1);

    const std::string fileName = path.substr(start, end - start);
    if (fileName == "." || fileName == "..") {
        return fileName;
    }

    const std::size_t dot = fileName.rfind('.');
    if (dot == std::string::npos) {
        return fileName;
    }
    if (dot == 0) {
        return fileName;
    }
    return fileName.substr(0, dot);
}

std::string applicationNameLikeQtDefault()
{
    const std::string argv0 = proc_self_cmdline_argv0();
    return basename_qt_fileinfo_basename_semantics(argv0);
}

CommandRunner::QuietMode toQuietMode(CmdCpp::QuietMode quiet)
{
    return (quiet == CmdCpp::QuietMode::Yes) ? CommandRunner::QuietMode::Yes : CommandRunner::QuietMode::No;
}
} // namespace

CmdCpp::CmdCpp()
    : elevationToolPath(CmdCpp::elevationTool())
    , helperPath(std::string("/usr/lib/") + applicationNameLikeQtDefault() + std::string("/helper"))
{
}

std::string CmdCpp::elevationTool()
{
    return CommandRunner::elevationTool();
}

bool CmdCpp::isCliMode()
{
    return true;
}

std::string CmdCpp::loggedInUserName()
{
    return CommandRunner::loggedInUserName();
}

std::vector<std::string> CmdCpp::helperExecArgs(const std::string &program, const std::vector<std::string> &args)
{
    std::vector<std::string> out;
    out.reserve(static_cast<std::size_t>(2 + args.size()));
    out.push_back(std::string("exec"));
    out.push_back(program);
    for (const std::string &a : args) {
        out.push_back(a);
    }
    return out;
}

std::string CmdCpp::trimLikeQString(const std::string &s)
{
    std::size_t b = 0;
    while (b < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[b]);
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f' || c == '\v') {
            ++b;
        } else {
            break;
        }
    }

    std::size_t e = s.size();
    while (e > b) {
        const unsigned char c = static_cast<unsigned char>(s[e - 1]);
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f' || c == '\v') {
            --e;
        } else {
            break;
        }
    }
    return s.substr(b, e - b);
}

std::string CmdCpp::getOut(const std::string &cmd, QuietMode quiet)
{
    (void)run(cmd, quiet);
    return trimLikeQString(outBuffer);
}

std::string CmdCpp::getOutAsRoot(const std::string &program, const std::vector<std::string> &args, QuietMode quiet)
{
    std::string output;
    (void)procAsRoot(program, args, &output, nullptr, quiet);
    return output;
}

bool CmdCpp::helperProc(const std::vector<std::string> &helperArgs, std::string *output, const std::string *input,
                        QuietMode quiet)
{
    if (getuid() != 0 && elevationToolPath.empty()) {
        // Matches Cmd (CLI_BUILD): warn and return false; no message box and no exit.
        // The Qt version logs:
        //   qWarning().noquote() << tr("No elevation tool found (pkexec/gksu/sudo).")
        // We keep the same human text.
        LoggerCpp::log(LoggerCpp::Level::Warning, std::string("No elevation tool found (pkexec/gksu/sudo)."));
        return false;
    }

    const std::string program = (getuid() == 0) ? helperPath : elevationToolPath;
    std::vector<std::string> programArgs = helperArgs;
    if (getuid() != 0) {
        programArgs.insert(programArgs.begin(), helperPath);
    }

    const bool ok = proc(program, programArgs, output, input, quiet, Elevation::No);
    if (lastExitCode == EXIT_CODE_PERMISSION_DENIED || lastExitCode == EXIT_CODE_COMMAND_NOT_FOUND) {
        handleElevationError();
    }
    return ok;
}

bool CmdCpp::proc(const std::string &program, const std::vector<std::string> &args, std::string *output,
                  const std::string *input, QuietMode quiet, Elevation elevation)
{
    if (elevation == Elevation::Yes) {
        return helperProc(helperExecArgs(program, args), output, input, quiet);
    }

    outBuffer.clear();

    const std::string stdinText = input ? *input : std::string();

    const CommandRunner::Result r = CommandRunner::proc(program, args, stdinText, toQuietMode(quiet),
                                                        CommandRunner::Elevation::No);

    outBuffer = trimLikeQString(r.mergedText);
    lastExitCode = r.exitCode;
    lastNormalExit = r.normalExit;

    if (output) {
        *output = outBuffer;
    }

    return (r.normalExit && r.exitCode == 0);
}

bool CmdCpp::procAsRoot(const std::string &program, const std::vector<std::string> &args, std::string *output,
                        const std::string *input, QuietMode quiet)
{
    return proc(program, args, output, input, quiet, Elevation::Yes);
}

std::string CmdCpp::readAllOutput() const
{
    return trimLikeQString(outBuffer);
}

bool CmdCpp::run(const std::string &cmd, QuietMode quiet)
{
    return proc("/bin/bash", {"-c", cmd}, nullptr, nullptr, quiet, Elevation::No);
}

void CmdCpp::handleElevationError() const
{
    // Matches Cmd hard-error path. In CLI_BUILD we show the user-facing message.
    // In GUI (non-CLI_BUILD), CmdCpp must not depend on Qt; the caller (GUI) can decide how to surface errors.
#ifdef CLI_BUILD
    MessageHandlerCpp::showMessage(
        MessageHandlerCpp::Critical,
        std::string("Administrator Access Required"),
        std::string("This operation requires administrator privileges. Please restart the application and enter your password when prompted."));
#else
    LoggerCpp::log(LoggerCpp::Level::Warning,
                   std::string("Administrator Access Required: This operation requires administrator privileges."));
#endif
}
