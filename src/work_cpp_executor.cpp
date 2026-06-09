#include "work_cpp_executor.h"

#include <chrono>

#include "embedded/embedded_assets.h"
#include "embedded/embedded_assets_runtime.h"
#include "datetime_cpp.h"
#include "dir_cpp.h"
#include "file_cpp.h"
#include "command_runner.h"
#include "process_runner.h"
#include "string_cpp.h"
#include "work_cpp_utils.h"
#ifdef UNIT_TESTS
#include "tempdir.h"
#endif

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
                    const std::string checkResultPrefix = "CHECK_RESULT installed-to-live ";
                    if (c.command.rfind(checkResultPrefix, 0) == 0) {
                        // Check if the last ProcAsRoot command (installed-to-live) failed
                        const bool failed = !(lastProcAsRootResult.started
                                             && lastProcAsRootResult.normalExit
                                             && lastProcAsRootResult.exitCode == 0);
                        if (failed) {
                            // Extract error message from CHECK_RESULT command
                            const std::string rest = c.command.substr(checkResultPrefix.size());
                            const std::string elseMarker = " else ERROR: ";
                            const std::size_t elsePos = rest.find(elseMarker);
                            if (elsePos != std::string::npos) {
                                const std::string errorMsg = rest.substr(elsePos + elseMarker.size());
                                if (cb.messageBox) {
                                    cb.messageBox(BoxType::critical, "Error", errorMsg);
                                }
                            }
                            // Qt calls cleanUp() here which does cleanup operations and exits
                            // For the C++ implementation, we need to execute cleanup steps inline
                            // This matches the Qt behavior of calling cleanUp() when installed-to-live fails
                            
                            const std::string appName = application_name_for_cleanup(cb);

                            (void)CommandRunner::procAsRoot("chown_conf",
                                                            {},
                                                            std::string(),
                                                            CommandRunner::QuietMode::Yes);
#ifdef UNIT_TESTS
                            (void)TempDir::removeRecursivelyForTests("<tempdir>");
#endif
                            (void)CommandRunner::procAsRoot("cleanup_overlay",
                                                            {appName},
                                                            std::string(),
                                                            CommandRunner::QuietMode::Yes);
                            
                            r.aborted = true;
                            r.abortReason = "installed-to-live command failed - cleanUp called";
                            return r;
                        }
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
                        } else {
                            const std::string replaceMenuStringsPrefix = "REPLACE_MENU_STRINGS ";
                        if (c.command.rfind(replaceMenuStringsPrefix, 0) == 0) {
                            // REPLACE_MENU_STRINGS <workDir>|<projectName>|<distroVersion>|<fullDistroName>|<releaseDate>|<codename>|<bootOptions>
                            const std::string params = c.command.substr(replaceMenuStringsPrefix.size());
                            
                            // Parse pipe-separated parameters
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
                            parts.push_back(current); // Add last part
                            
                            if (parts.size() >= 7) {
                                const std::string &workDir = parts[0];
                                const std::string &projectName = parts[1];
                                const std::string &distroVersion = parts[2];
                                const std::string &fullDistroName = parts[3];
                                const std::string &releaseDate = parts[4];
                                const std::string &codename = parts[5];
                                const std::string &bootOptions = parts[6];
                                
                                (void)WorkCppUtils::replaceMenuStrings(workDir, projectName, distroVersion,
                                                                       fullDistroName, releaseDate, codename, bootOptions);
                            }
                        } else {
                            const std::string md5ChecksumPrefix = "MD5_CHECKSUM ";
                            if (c.command.rfind(md5ChecksumPrefix, 0) == 0) {
                                // MD5_CHECKSUM <folder> <filename>
                                const std::string params = c.command.substr(md5ChecksumPrefix.size());
                                const std::size_t space = params.find(' ');
                                if (space != std::string::npos) {
                                    const std::string folder = params.substr(0, space);
                                    const std::string filename = params.substr(space + 1);
                                    
                                    // Execute: cd folder && md5sum filename > filename.md5
                                    const std::string cmd = "cd \"" + folder + "\" && md5sum \"" + filename + "\" > \"" + filename + ".md5\"";
                                    (void)CommandRunner::run(cmd, c.quietYes ? CommandRunner::QuietMode::Yes : CommandRunner::QuietMode::No);
                                }
                            } else {
                                const std::string embedIsoPrefix = "EMBED_EXTRACT_ISO_TEMPLATE ";
                                if (c.command.rfind(embedIsoPrefix, 0) == 0) {
                                    const std::string dest = strip_shell_quotes(c.command.substr(embedIsoPrefix.size()));
                                    const EmbeddedAssets::Result extract = EmbeddedAssets::extractIsoTemplateTree(dest);
                                    if (!extract.ok) {
                                        r.aborted = true;
                                        r.abortReason = std::string("embedded iso-template extraction failed: ") + extract.error;
                                        return r;
                                    }
                                } else {
                                const std::string embedInitrdPrefix = "EMBED_POPULATE_INITRD_DIR ";
                                if (c.command.rfind(embedInitrdPrefix, 0) == 0) {
                                    const std::string dest = strip_shell_quotes(c.command.substr(embedInitrdPrefix.size()));
                                    (void)DirCpp().mkpath(dest);
                                    (void)CommandRunner::run("chmod a+rx \"" + dest + "\"", CommandRunner::QuietMode::Yes);
                                    const EmbeddedAssets::Result extract = EmbeddedAssets::extractTemplateInitrd(dest);
                                    if (!extract.ok) {
                                        r.aborted = true;
                                        r.abortReason = std::string("embedded template-initrd extraction failed: ") + extract.error;
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
                                } else {
                                    lastRunCommandLineSuccess = CommandRunner::run(c.command,
                                                                                   c.quietYes ? CommandRunner::QuietMode::Yes
                                                                                              : CommandRunner::QuietMode::No);
                                }
                                }
                            }
                        }
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
                r.aborted = true;
                const std::string detail = StringCpp::trimmedLikeQStringUtf8(lastProcAsRootResult.mergedText);
                r.abortReason = detail.empty()
                    ? (std::string("Privileged operation failed: ") + c.program)
                    : detail;
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
            (void)FileCpp::copy(c.source, c.destination);
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
#ifdef UNIT_TESTS
            // We don't know the actual path at plan time; record deterministically via hooks.
            (void)TempDir::removeRecursivelyForTests("<tempdir>");
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
