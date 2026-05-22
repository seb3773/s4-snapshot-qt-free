/**********************************************************************
 *  helper.cpp
 **********************************************************************
 * Copyright (C) 2026 MX Authors
 *
 * Authors: Adrian
 *          Debian <http://debian.org>
 *          OpenAI Codex
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

#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unordered_map>
#include <unistd.h>
#include <vector>

#include "file_cpp.h"
#include "process_runner.h"

namespace
{
struct ProcessResult
{
    bool started = false;
    int exitCode = 1;
    ProcessRunner::ExitStatus exitStatus = ProcessRunner::ExitStatus::NormalExit;
};

void writeAndFlush(FILE *stream, const char *data, size_t n)
{
    if (data && n > 0) {
        std::fwrite(data, 1, n, stream);
        std::fflush(stream);
    }
}

void printError(const std::string &message)
{
    writeAndFlush(stderr, message.data(), message.size());
    writeAndFlush(stderr, "\n", 1);
}

[[nodiscard]] const std::unordered_map<std::string, std::vector<std::string>> &allowedCommands()
{
    static const std::unordered_map<std::string, std::vector<std::string>> commands {
        {"apt-get", {"/usr/bin/apt-get"}},
        {"chown", {"/usr/bin/chown", "/bin/chown"}},
        {"copy-initrd-programs",
         {"/usr/share/s4-snapshot/scripts/copy-initrd-programs",
          "/usr/share/iso-snapshot-cli/scripts/copy-initrd-programs"}},
        {"du", {"/usr/bin/du", "/bin/du"}},
        {"installed-to-live", {"/usr/sbin/installed-to-live", "/usr/bin/installed-to-live"}},
        {"mkdir", {"/usr/bin/mkdir", "/bin/mkdir"}},
        {"mount", {"/usr/bin/mount", "/bin/mount"}},
        {"mountpoint", {"/usr/bin/mountpoint", "/bin/mountpoint"}},
        {"mksquashfs", {"/usr/bin/mksquashfs", "/bin/mksquashfs"}},  // Phase 9: Add /bin path
        {"runuser", {"/usr/sbin/runuser", "/sbin/runuser", "/usr/bin/runuser"}},
        {"stdbuf", {"/usr/bin/stdbuf", "/bin/stdbuf"}},  // Phase 9: Add /bin path
        {"true", {"/usr/bin/true", "/bin/true"}},
        {"umount", {"/usr/bin/umount", "/bin/umount"}},
        {"unbuffer", {"/usr/bin/unbuffer", "/bin/unbuffer"}},  // Phase 9: Add /bin path
    };
    return commands;
}

[[nodiscard]] std::string resolveBinary(const std::vector<std::string> &candidates)
{
    for (const std::string &candidate : candidates) {
        if (FileCpp::exists(candidate) && ::access(candidate.c_str(), X_OK) == 0) {
            return candidate;
        }
    }
    return std::string();
}

[[nodiscard]] ProcessResult runProcess(const std::string &program, const std::vector<std::string> &args,
                                       const std::string &input = std::string())
{
    ProcessResult result;

    const ProcessRunner::Result r = ProcessRunner::runStreaming(
        program,
        args,
        input,
        [](const char *data, size_t n) { writeAndFlush(stdout, data, n); },
        [](const char *data, size_t n) { writeAndFlush(stderr, data, n); },
        -1);

    result.started = r.started;
    result.exitStatus = r.exitStatus;
    result.exitCode = r.exitCode;
    if (!result.started) {
        const std::string msg = std::string("Failed to start ") + program + "\n";
        writeAndFlush(stderr, msg.data(), msg.size());
        result.exitCode = 127;
    }
    return result;
}

[[nodiscard]] int relayResult(const ProcessResult &result)
{
    if (!result.started) {
        return result.exitCode;
    }
    return result.exitStatus == ProcessRunner::ExitStatus::NormalExit ? result.exitCode : 1;
}

[[nodiscard]] std::string readHelperInput()
{
    FileCpp f;
    f.setFileName("/proc/self/fd/0");
    if (!f.open(FileCpp::OpenMode::ReadOnly)) {
        return std::string();
    }
    const std::vector<std::uint8_t> data = f.readAll();
    return std::string(reinterpret_cast<const char *>(data.data()), data.size());
}

[[nodiscard]] int runAllowedCommand(const std::string &command, const std::vector<std::string> &commandArgs,
                                    const std::string &input = std::string())
{
    const auto commandIt = allowedCommands().find(command);
    if (commandIt == allowedCommands().end()) {
        printError(std::string("Command is not allowed: ") + command);
        return 127;
    }

    const std::string resolvedCommand = resolveBinary(commandIt->second);
    if (resolvedCommand.empty()) {
        printError(std::string("Command is not available: ") + command);
        return 127;
    }

    return relayResult(runProcess(resolvedCommand, commandArgs, input));
}
} // namespace

int main(int argc, char *argv[])
{
    ::umask(0022);

    std::vector<std::string> arguments;
    arguments.reserve(argc > 1 ? static_cast<size_t>(argc - 1) : 0U);
    for (int i = 1; i < argc; ++i) {
        arguments.emplace_back(argv[i] ? argv[i] : "");
    }
    if (arguments.empty()) {
        printError("Missing helper action");
        return 1;
    }

    const std::string action = arguments.front();
    arguments.erase(arguments.begin());
    if (action != "exec") {
        printError(std::string("Unsupported helper action: ") + action);
        return 1;
    }
    if (arguments.empty()) {
        printError("exec requires a command name");
        return 1;
    }

    const std::string command = arguments.front();
    std::vector<std::string> commandArgs;
    if (arguments.size() > 1) {
        commandArgs.assign(arguments.begin() + 1, arguments.end());
    }
    return runAllowedCommand(command, commandArgs, readHelperInput());
}
