/**********************************************************************
 *  helper.cpp
 **********************************************************************
 * Copyright (C) 2026 MX Authors
 *
 * Authors: Adrian
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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <unordered_map>
#include <unistd.h>
#include <vector>

#include "file_cpp.h"
#include "installed_to_live_cpp.h"
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
        {"mkdir", {"/usr/bin/mkdir", "/bin/mkdir"}},
        {"mount", {"/usr/bin/mount", "/bin/mount"}},
        {"mountpoint", {"/usr/bin/mountpoint", "/bin/mountpoint"}},
        {"mksquashfs", {"/usr/bin/mksquashfs", "/bin/mksquashfs"}},  // Phase 9: Add /bin path
        {"runuser", {"/usr/sbin/runuser", "/sbin/runuser", "/usr/bin/runuser"}},
        {"stdbuf", {"/usr/bin/stdbuf", "/bin/stdbuf"}},  // Phase 9: Add /bin path
        {"touch", {"/usr/bin/touch", "/bin/touch"}},
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

[[nodiscard]] bool isValidAppName(const std::string &value)
{
    if (value.empty()) {
        return false;
    }
    for (const char ch : value) {
        const bool ok = (ch >= 'A' && ch <= 'Z')
            || (ch >= 'a' && ch <= 'z')
            || (ch >= '0' && ch <= '9')
            || ch == '.'
            || ch == '_'
            || ch == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string loggedInUserName()
{
    const char *sudoUser = std::getenv("SUDO_USER");
    if (sudoUser != nullptr && std::strcmp(sudoUser, "root") != 0) {
        return sudoUser;
    }

    const char *logName = std::getenv("LOGNAME");
    if (logName != nullptr && std::strcmp(logName, "root") != 0) {
        return logName;
    }

    const char *user = std::getenv("USER");
    if (user != nullptr && std::strcmp(user, "root") != 0) {
        return user;
    }

    const std::string lognamePath = resolveBinary({"/usr/bin/logname", "/bin/logname"});
    if (!lognamePath.empty()) {
        const ProcessRunner::Result r = ProcessRunner::run(lognamePath, {});
        if (r.started && r.exitStatus == ProcessRunner::ExitStatus::NormalExit && r.exitCode == 0) {
            std::string name = r.stdoutText;
            while (!name.empty() && (name.back() == '\n' || name.back() == '\r' || name.back() == ' ' || name.back() == '\t')) {
                name.pop_back();
            }
            if (!name.empty() && name != "root") {
                return name;
            }
        }
    }

    return std::string();
}

[[nodiscard]] int killMksquashfs()
{
    const std::string pkillPath = resolveBinary({"/usr/bin/pkill", "/bin/pkill"});
    if (pkillPath.empty()) {
        return 0;
    }
    (void)runProcess(pkillPath, {"mksquashfs"});
    return 0;
}

[[nodiscard]] int cleanup()
{
    (void)killMksquashfs();
    return InstalledToLiveCpp::run({"cleanup"});
}

[[nodiscard]] int cleanupOverlay(const std::vector<std::string> &args)
{
    if (args.empty() || !isValidAppName(args.front())) {
        return 2;
    }

    const std::string overlayBase = std::string("/run/") + args.front() + "/bind-root-overlay";
    const std::string lowerDir = overlayBase + "/lower";
    const std::string overlayRoot = overlayBase + "/root";

    const std::string mountpointPath = resolveBinary({"/usr/bin/mountpoint", "/bin/mountpoint"});
    const std::string umountPath = resolveBinary({"/usr/bin/umount", "/bin/umount"});
    if (!mountpointPath.empty() && !umountPath.empty()) {
        if (relayResult(runProcess(mountpointPath, {"-q", overlayRoot})) == 0) {
            (void)runProcess(umountPath, {"--recursive", overlayRoot});
        }
        if (relayResult(runProcess(mountpointPath, {"-q", lowerDir})) == 0) {
            (void)runProcess(umountPath, {"--recursive", lowerDir});
        }
    }

    std::error_code ec;
    if (std::filesystem::is_symlink(overlayBase, ec)) {
        return 3;
    }
    if (std::filesystem::is_directory(overlayBase, ec)) {
        std::filesystem::remove_all(overlayBase, ec);
        if (ec) {
            printError(std::string("Failed to remove overlay workspace: ") + ec.message());
            return 1;
        }
    }

    return 0;
}

[[nodiscard]] int copyLog()
{
    for (const std::string &app : {std::string("iso-snapshot-cli"), std::string("s4-snapshot")}) {
        const std::string source = "/tmp/" + app + ".log";
        const std::string dest = "/var/log/" + app + ".log";
        if (!FileCpp::exists(source)) {
            continue;
        }
        if (FileCpp::exists(dest)) {
            (void)std::rename(dest.c_str(), (dest + ".old").c_str());
        }
        return FileCpp::copy(source, dest) ? 0 : 1;
    }
    return 0;
}

[[nodiscard]] int datetimeLog()
{
    std::time_t now = std::time(nullptr);
    std::tm tm {};
    if (localtime_r(&now, &tm) == nullptr) {
        return 1;
    }

    char buffer[32] {};
    if (std::strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M\n", &tm) == 0) {
        return 1;
    }

    FileCpp f("/etc/snapshot_created");
    if (!f.open(FileCpp::OpenMode::WriteOnly | FileCpp::OpenMode::Truncate)) {
        return 1;
    }
    return f.write(std::string(buffer)) >= 0 && f.flush() ? 0 : 1;
}

[[nodiscard]] int dropCaches()
{
    FileCpp f("/proc/sys/vm/drop_caches");
    if (!f.open(FileCpp::OpenMode::WriteOnly | FileCpp::OpenMode::Truncate)) {
        return 1;
    }
    return f.write("1\n") >= 0 && f.flush() ? 0 : 1;
}

[[nodiscard]] int runAllowedCommand(const std::string &command, const std::vector<std::string> &commandArgs,
                                    const std::string &input);

[[nodiscard]] int chownConf()
{
    const std::string username = loggedInUserName();
    if (username.empty()) {
        return 0;
    }

    for (const std::string &app : {std::string("iso-snapshot-cli"), std::string("s4-snapshot")}) {
        const std::string fileName = "/home/" + username + "/.config/MX-Linux/" + app + ".conf";
        if (FileCpp::exists(fileName)) {
            (void)runAllowedCommand("chown", {username + ":", fileName}, std::string());
        }
    }
    return 0;
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
    if (command == "chown_conf") {
        return chownConf();
    }
    if (command == "cleanup") {
        return cleanup();
    }
    if (command == "cleanup_overlay") {
        return cleanupOverlay(commandArgs);
    }
    if (command == "copy_log") {
        return copyLog();
    }
    if (command == "datetime_log") {
        return datetimeLog();
    }
    if (command == "kill_mksquashfs") {
        return killMksquashfs();
    }
    if (command == "drop_caches") {
        return dropCaches();
    }

    if (command == "installed-to-live") {
        return InstalledToLiveCpp::run(commandArgs);
    }

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
