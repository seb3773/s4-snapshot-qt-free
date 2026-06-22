#include "work_cpp_executor.h"

#include <chrono>
#include <cstdio>
#include <sstream>
#include <vector>

#include "embedded/embedded_assets.h"
#include "embedded/embedded_assets_runtime.h"
#include "datetime_cpp.h"
#include "dir_cpp.h"
#include "file_cpp.h"
#include "command_runner.h"
#include "process_runner.h"
#include "string_cpp.h"
#include "work_cpp_utils.h"
#include "stdio_cpp.h"
#include "tempdir.h"

namespace {

std::string application_name_for_cleanup(const WorkCppExecutor::Callbacks &cb)
{
    if (!cb.applicationName.empty()) {
        return cb.applicationName;
    }
#ifdef UNIT_TESTS
    return "unit_tests";
#else
    return "s4-snapshot";
#endif
}

bool save_package_list_like_qt(const std::string &outputPath)
{
    const ProcessRunner::Result dpkgList = ProcessRunner::run("dpkg", {"-l"}, std::string(), -1);
    if (!dpkgList.started || dpkgList.exitStatus != ProcessRunner::ExitStatus::NormalExit) {
        return false;
    }

    std::string body;
    std::istringstream in(dpkgList.stdoutText);
    std::string line;
    while (std::getline(in, line)) {
        if (line.size() < 4 || line.rfind("ii ", 0) != 0) {
            continue;
        }
        std::istringstream ls(line);
        std::string status;
        std::string package;
        std::string version;
        ls >> status >> package >> version;
        if (package.empty() || version.empty()) {
            continue;
        }
        char formatted[128];
        std::snprintf(formatted, sizeof(formatted), "%-41s %s\n", package.c_str(), version.c_str());
        body += formatted;
    }

    return WorkCppUtils::writeTextFileUtf8NoBomTruncate(outputPath, body);
}

std::string format_proc_command_line(const std::string &program, const std::vector<std::string> &args)
{
    std::string line = program;
    for (const std::string &arg : args) {
        line += ' ';
        line += WorkCppUtils::shellQuoteSingleQuoted(arg);
    }
    return line;
}

} // namespace

static std::string strip_shell_quotes(const std::string &value)
{
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

WorkCppExecutor::Result WorkCppExecutor::run(const WorkCppPlan &plan, const Callbacks &cb)
{
    Result r;
    CommandRunner::Result lastProcAsRootResult;
    bool lastRunCommandLineSuccess = true;  // Track last RunCommandLine result
    
    // Track start time for elapsed time calculation
    const auto startTime = std::chrono::steady_clock::now();

    for (const WorkCppPlanStep &st : plan.steps) {
        if (EmbeddedAssetsRuntime::signalStopRequested()) {
            (void)EmbeddedAssetsRuntime::handleSignalStopIfRequested();
            r.aborted = true;
            r.abortReason = "Operation interrupted";
            return r;
        }

        if (std::holds_alternative<WorkCppPlanStep::Message>(st.payload)) {
            if (cb.message) {
                cb.message(std::get<WorkCppPlanStep::Message>(st.payload).text);
            }
            continue;
        }

        if (std::holds_alternative<WorkCppPlanStep::MessageBox>(st.payload)) {
            if (cb.messageBox) {
                const auto &m = std::get<WorkCppPlanStep::MessageBox>(st.payload);
                std::string text = m.text;
                
                // Replace <ELAPSED> placeholder with actual elapsed time
                const std::string placeholder = "<ELAPSED>";
                const size_t pos = text.find(placeholder);
                if (pos != std::string::npos) {
                    const auto now = std::chrono::steady_clock::now();
                    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime);
                    const std::string formatted = DateTimeCpp::formatElapsedTime(elapsed.count());
                    text.replace(pos, placeholder.length(), formatted);
                }
                
                cb.messageBox(m.type, m.title, text);
            }
            continue;
        }

        if (std::holds_alternative<WorkCppPlanStep::RunCommandLine>(st.payload)) {
            const auto &c = std::get<WorkCppPlanStep::RunCommandLine>(st.payload);
            const std::string prefix = "WRITE_TEXT_FILE_UTF8_TRUNCATE ";
            if (c.command.rfind(prefix, 0) == 0) {
                const std::string rest = c.command.substr(prefix.size());
                const std::size_t sep = rest.find(' ');
                if (sep != std::string::npos) {
                    const std::string filePath = rest.substr(0, sep);
                    const std::string text = rest.substr(sep + 1);
                    (void)WorkCppUtils::writeTextFileUtf8NoBomTruncate(filePath, text);
                }
            } else {
                const std::string linuxfsInfoPrefix = "WRITE_LINUXFS_INFO_FROM_MKSQUASHFS_OUTPUT ";
                if (c.command.rfind(linuxfsInfoPrefix, 0) == 0) {
                    // Extract linuxfs.info path from command
                    const std::string linuxfsInfoPath = c.command.substr(linuxfsInfoPrefix.size());
                    // Use output from last ProcAsRoot command (mksquashfs)
                    (void)WorkCppUtils::writeLinuxfsInfoFromMksquashfsOutput(
                        lastProcAsRootResult.mergedText, linuxfsInfoPath);
                } else {
                    enum class ProcAsRootCheckOutcome { NoMatch, Ok, Failed };

                    const auto check_proc_as_root = [&](const std::string &checkPrefix,
                                                        const std::string &abortReason) -> ProcAsRootCheckOutcome {
                        if (c.command.rfind(checkPrefix, 0) != 0) {
                            return ProcAsRootCheckOutcome::NoMatch;
                        }
                        const bool failed = !(lastProcAsRootResult.started
                                              && lastProcAsRootResult.normalExit
                                              && lastProcAsRootResult.exitCode == 0);
                        if (!failed) {
                            return ProcAsRootCheckOutcome::Ok;
                        }
                        const std::string rest = c.command.substr(checkPrefix.size());
                        const std::string elseMarker = " else ERROR: ";
                        const std::size_t elsePos = rest.find(elseMarker);
                        if (elsePos != std::string::npos) {
                            const std::string errorMsg = rest.substr(elsePos + elseMarker.size());
                            if (cb.messageBox) {
                                cb.messageBox(BoxType::critical, "Error", errorMsg);
                            }
                        }
                        (void)CommandRunner::procAsRoot("chown_conf",
                                                        {},
                                                        std::string(),
                                                        CommandRunner::QuietMode::Yes);
#ifdef UNIT_TESTS
                        (void)TempDir::removeRecursivelyForTests("<tempdir>");
#endif
                        (void)CommandRunner::procAsRoot("cleanup_overlay",
                                                        {application_name_for_cleanup(cb)},
                                                        std::string(),
                                                        CommandRunner::QuietMode::Yes);
                        r.aborted = true;
                        r.abortReason = abortReason;
                        return ProcAsRootCheckOutcome::Failed;
                    };

                    const ProcAsRootCheckOutcome installedToLiveCheck = check_proc_as_root(
                        "CHECK_RESULT installed-to-live ",
                        "installed-to-live command failed - cleanUp called");
                    if (installedToLiveCheck == ProcAsRootCheckOutcome::Failed) {
                        return r;
                    }

                    const ProcAsRootCheckOutcome overlayBindCheck = check_proc_as_root(
                        "CHECK_RESULT overlay_bind_mount ",
                        "bind-root overlay bind mount failed - cleanUp called");
                    if (overlayBindCheck == ProcAsRootCheckOutcome::Failed) {
                        return r;
                    }

                    const ProcAsRootCheckOutcome overlayMountCheck = check_proc_as_root(
                        "CHECK_RESULT overlay_mount ",
                        "bind-root overlay mount failed - cleanUp called");
                    if (overlayMountCheck == ProcAsRootCheckOutcome::Failed) {
                        return r;
                    }

                    const ProcAsRootCheckOutcome embedInstallerCheck = check_proc_as_root(
                        "CHECK_RESULT embed_installer ",
                        "embed_installer command failed - cleanUp called");
                    if (embedInstallerCheck == ProcAsRootCheckOutcome::Failed) {
                        return r;
                    }

                    if (installedToLiveCheck == ProcAsRootCheckOutcome::Ok
                        || overlayBindCheck == ProcAsRootCheckOutcome::Ok
                        || overlayMountCheck == ProcAsRootCheckOutcome::Ok
                        || embedInstallerCheck == ProcAsRootCheckOutcome::Ok) {
                        // Matched a proc-as-root CHECK_RESULT and the command succeeded.
                    } else {
                        // Check for generic CHECK_RESULT (for regular commands like xorriso)
                        const std::string checkResultGenericPrefix = "CHECK_RESULT ";
                        if (c.command.rfind(checkResultGenericPrefix, 0) == 0) {
                            // Check if the last RunCommandLine failed
                            if (!lastRunCommandLineSuccess) {
                                // Extract error message from CHECK_RESULT command
                                const std::string rest = c.command.substr(checkResultGenericPrefix.size());
                                const std::string elseMarker = " else ERROR: ";
                                const std::size_t elsePos = rest.find(elseMarker);
                                if (elsePos != std::string::npos) {
                                    const std::string errorMsg = rest.substr(elsePos + elseMarker.size());
                                    if (cb.messageBox) {
                                        cb.messageBox(BoxType::critical, "Error", errorMsg);
                                    }
                                }
                                r.aborted = true;
                                r.abortReason = "Command failed - CHECK_RESULT triggered abort";
                                return r;
                            }
                        } else if (c.command == "STDIO_WRITE_STDOUT \\033[?25h") {
                            (void)StdioCpp::write(stdout, "\033[?25h");
                            lastRunCommandLineSuccess = true;
                        } else if (c.command == "STDIO_FLUSH_STDOUT") {
                            (void)StdioCpp::flush(stdout);
                            lastRunCommandLineSuccess = true;
                        } else if (c.command.rfind("SAVE_PACKAGE_LIST ", 0) == 0) {
                            const std::string outputPath = c.command.substr(std::string("SAVE_PACKAGE_LIST ").size());
                            lastRunCommandLineSuccess = save_package_list_like_qt(outputPath);
                        } else if (c.command.rfind("REPLACE_MENU_STRINGS ", 0) == 0) {
                            const std::string params = c.command.substr(std::string("REPLACE_MENU_STRINGS ").size());
                            std::vector<std::string> parts;
                            std::string current;
                            for (char ch : params) {
                                if (ch == '|') {
                                    parts.push_back(current);
                                    current.clear();
                                } else {
                                    current.push_back(ch);
                                }
                            }
                            parts.push_back(current);

                            if (parts.size() >= 7) {
                                (void)WorkCppUtils::replaceMenuStrings(parts[0],
                                                                       parts[1],
                                                                       parts[2],
                                                                       parts[3],
                                                                       parts[4],
                                                                       parts[5],
                                                                       parts[6]);
                            }
                            lastRunCommandLineSuccess = true;
                        } else if (c.command.rfind("FSYNC_FILE ", 0) == 0) {
                            const std::string path = c.command.substr(std::string("FSYNC_FILE ").size());
                            lastRunCommandLineSuccess = FileCpp::fsyncPath(path);
                        } else if (c.command.rfind("MD5_CHECKSUM ", 0) == 0) {
                            const std::string params = c.command.substr(std::string("MD5_CHECKSUM ").size());
                            const std::size_t space = params.find(' ');
                            if (space != std::string::npos) {
                                const std::string folder = params.substr(0, space);
                                const std::string filename = params.substr(space + 1);
                                const std::string cmd = "cd \"" + folder + "\" && md5sum \"" + filename + "\" > \""
                                    + filename + ".md5\"";
                                lastRunCommandLineSuccess = CommandRunner::run(
                                    cmd, c.quietYes ? CommandRunner::QuietMode::Yes : CommandRunner::QuietMode::No);
                            }
                        } else if (c.command.rfind("EMBED_EXTRACT_ISO_TEMPLATE ", 0) == 0) {
                            const std::string dest = strip_shell_quotes(
                                c.command.substr(std::string("EMBED_EXTRACT_ISO_TEMPLATE ").size()));
                            const EmbeddedAssets::Result extract = EmbeddedAssets::extractIsoTemplateTree(dest);
                            if (!extract.ok) {
                                r.aborted = true;
                                r.abortReason = std::string("embedded iso-template extraction failed: ") + extract.error;
                                return r;
                            }
                            lastRunCommandLineSuccess = true;
                        } else if (c.command.rfind("EMBED_POPULATE_INITRD_DIR ", 0) == 0) {
                            const std::string dest = strip_shell_quotes(
                                c.command.substr(std::string("EMBED_POPULATE_INITRD_DIR ").size()));
                            (void)DirCpp().mkpath(dest);
                            (void)CommandRunner::run("chmod a+rx \"" + dest + "\"", CommandRunner::QuietMode::Yes);
                            const EmbeddedAssets::Result extract = EmbeddedAssets::extractTemplateInitrd(dest);
                            if (!extract.ok) {
                                r.aborted = true;
                                r.abortReason = std::string("embedded template-initrd extraction failed: ")
                                    + extract.error;
                                return r;
                            }
                            const EmbeddedAssets::Result scriptsExtract = EmbeddedAssets::extractRuntimeScripts(
                                EmbeddedAssetsRuntime::initrdScriptsDir(dest));
                            if (!scriptsExtract.ok) {
                                r.aborted = true;
                                r.abortReason = std::string("embedded initrd scripts extraction failed: ")
                                    + scriptsExtract.error;
                                return r;
                            }
                            lastRunCommandLineSuccess = true;
                        } else {
                            lastRunCommandLineSuccess = CommandRunner::run(
                                c.command, c.quietYes ? CommandRunner::QuietMode::Yes : CommandRunner::QuietMode::No);
                        }
                    }
                }
            }
            continue;
        }

        if (std::holds_alternative<WorkCppPlanStep::ProcAsRoot>(st.payload)) {
            const auto &c = std::get<WorkCppPlanStep::ProcAsRoot>(st.payload);
            lastProcAsRootResult = CommandRunner::procAsRoot(c.program, c.args, std::string(),
                                                             c.quietYes ? CommandRunner::QuietMode::Yes
                                                                        : CommandRunner::QuietMode::No);
            if (!(lastProcAsRootResult.started && lastProcAsRootResult.normalExit
                  && lastProcAsRootResult.exitCode == 0)) {
                if (EmbeddedAssetsRuntime::signalStopRequested()) {
                    r.aborted = true;
                    r.abortReason = "Operation interrupted";
                    return r;
                }
                // WorkQtOracle checks these inline and continues to CHECK_RESULT / messageBox /
                // cleanUp; match that instead of aborting before the next planned step.
                if (c.program == "mount" || c.program == "umount" || c.program == "mountpoint"
                    || c.program == "installed-to-live" || c.program == "embed_installer") {
                    continue;
                }
                r.aborted = true;
                const std::string cmdLine = format_proc_command_line(c.program, c.args);
                const std::string detail = StringCpp::trimmedLikeQStringUtf8(lastProcAsRootResult.mergedText);
                if (detail.empty()) {
                    r.abortReason = std::string("Privileged operation failed: ") + cmdLine
                        + " (exit " + std::to_string(lastProcAsRootResult.exitCode) + ")";
                } else {
                    r.abortReason = std::string("Privileged operation failed: ") + cmdLine + "\n" + detail;
                }
                return r;
            }
            continue;
        }

        if (std::holds_alternative<WorkCppPlanStep::ProcessExecute>(st.payload)) {
            const auto &c = std::get<WorkCppPlanStep::ProcessExecute>(st.payload);
            (void)ProcessRunner::execute(c.program, c.args, c.timeoutMs);
            continue;
        }

        if (std::holds_alternative<WorkCppPlanStep::FileCopy>(st.payload)) {
            const auto &c = std::get<WorkCppPlanStep::FileCopy>(st.payload);
            lastRunCommandLineSuccess = FileCpp::copy(c.source, c.destination, c.fsyncAfter);
            continue;
        }

        if (std::holds_alternative<WorkCppPlanStep::FileRemove>(st.payload)) {
            const auto &c = std::get<WorkCppPlanStep::FileRemove>(st.payload);
            (void)FileCpp::remove(c.path);
            continue;
        }

        if (std::holds_alternative<WorkCppPlanStep::DirRemoveRecursively>(st.payload)) {
            const auto &c = std::get<WorkCppPlanStep::DirRemoveRecursively>(st.payload);
            DirCpp d(c.path);
            (void)d.removeRecursively();
            continue;
        }

        if (std::holds_alternative<WorkCppPlanStep::Mkpath>(st.payload)) {
            const auto &c = std::get<WorkCppPlanStep::Mkpath>(st.payload);
            DirCpp d;
            (void)d.mkpath(c.path);
            continue;
        }

        if (std::holds_alternative<WorkCppPlanStep::Chdir>(st.payload)) {
            const auto &c = std::get<WorkCppPlanStep::Chdir>(st.payload);
            (void)DirCpp::setCurrent(c.path);
            continue;
        }

        if (std::holds_alternative<WorkCppPlanStep::TempDirRemove>(st.payload)) {
            const auto &c = std::get<WorkCppPlanStep::TempDirRemove>(st.payload);
            if (cb.removeTempDir) {
                (void)cb.removeTempDir(c.debugName);
            } else if (c.debugName == "initrd_dir" && !cb.initrdTempDirPath.empty()) {
                DirCpp d(cb.initrdTempDirPath);
                (void)d.removeRecursively();
            }
#ifdef UNIT_TESTS
            else {
                (void)TempDir::removeRecursivelyForTests("<tempdir>");
            }
#endif
            continue;
        }

        if (std::holds_alternative<WorkCppPlanStep::Abort>(st.payload)) {
            if (!r.aborted) {
                r.aborted = true;
                r.abortReason = std::get<WorkCppPlanStep::Abort>(st.payload).reason;
            }
            continue;
        }
    }

    return r;
}
