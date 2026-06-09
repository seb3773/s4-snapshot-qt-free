#include "settings_editor_cpp.h"

#include <sys/types.h>
#include <unistd.h>

#include <cstdlib>
#include <vector>

#include "command_runner.h"
#include "file_cpp.h"
#include "standard_paths_cpp.h"
#include "string_cpp.h"
#include "work_cpp_utils.h"

namespace {

static std::vector<std::string> build_executable_search_path_like_settings_qt()
{
    const char *env = std::getenv("PATH");
    const std::string pathEnv = (env != nullptr ? std::string(env) : std::string());
    std::vector<std::string> out = StringCpp::splitLikeQString(pathEnv, ":", StringCpp::SplitBehavior::KeepEmptyParts);
    out.push_back("/usr/sbin");
    return out;
}

static std::string trim_like_qt(const std::string &s)
{
    return StringCpp::trimmedLikeQStringUtf8(s);
}

static bool starts_with_exec_prefix(const std::string &line)
{
    return StringCpp::startsWithLikeQStringUtf8(line, "Exec=");
}

static std::string remove_prefix_exec(const std::string &line)
{
    return StringCpp::removeLikeQStringUtf8(line, 0, static_cast<int>(std::string("Exec=").size()));
}

static void replace_all_in_place(std::string &s, const std::string &from, const std::string &to)
{
    if (from.empty()) {
        return;
    }
    std::size_t pos = 0;
    while (true) {
        pos = s.find(from, pos);
        if (pos == std::string::npos) {
            break;
        }
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

static std::string parse_exec_line_like_settings_qt(const std::string &lineBytes)
{
    if (!starts_with_exec_prefix(lineBytes)) {
        return {};
    }

    std::string line = remove_prefix_exec(lineBytes);

    replace_all_in_place(line, "%u", "");
    replace_all_in_place(line, "%U", "");
    replace_all_in_place(line, "%f", "");
    replace_all_in_place(line, "%F", "");
    replace_all_in_place(line, "%c", "");
    replace_all_in_place(line, "%C", "");
    replace_all_in_place(line, "-b", "");

    return trim_like_qt(line);
}

static bool ends_with_like_qt(const std::string &s, const std::string &suffix)
{
    return StringCpp::endsWithLikeQStringUtf8(s, suffix);
}

static bool contains_like_qt(const std::string &s, const std::string &sub)
{
    return s.find(sub) != std::string::npos;
}

} // namespace

std::string SettingsEditorCpp::getEditorLikeSettingsQt(const std::string &guiEditor)
{
    const std::vector<std::string> path = build_executable_search_path_like_settings_qt();

    std::string editor = guiEditor;
    if (editor.empty() || WorkCppUtils::findExecutable(editor, path).empty()) {
        const std::string defaultEditor = CommandRunner::getOut("xdg-mime query default text/plain", CommandRunner::QuietMode::Yes);
        const std::string desktopFile = StandardPathsCpp::locateApplicationsFile(defaultEditor);

        FileCpp file(desktopFile);
        if (file.open(FileCpp::OpenMode::ReadOnly)) {
            while (true) {
                const std::string lineBytes = file.readLine();
                if (lineBytes.empty()) {
                    break;
                }
                const std::string parsed = parse_exec_line_like_settings_qt(lineBytes);
                if (!parsed.empty()) {
                    editor = parsed;
                    break;
                }
            }
            file.close();
        }

        if (editor.empty()) {
            editor = "nano";
        }
    }

    const bool isRoot = ::getuid() == 0;
    const bool isEditorThatElevates = ends_with_like_qt(editor, "kate")
        || ends_with_like_qt(editor, "kwrite")
        || ends_with_like_qt(editor, "featherpad")
        || ends_with_like_qt(editor, "code")
        || ends_with_like_qt(editor, "codium");

    const bool isCliEditor = contains_like_qt(editor, "nano")
        || contains_like_qt(editor, "vi")
        || contains_like_qt(editor, "vim")
        || contains_like_qt(editor, "nvim")
        || contains_like_qt(editor, "micro")
        || contains_like_qt(editor, "emacs");

    std::string elevate = CommandRunner::elevationTool();

    if (isEditorThatElevates && !isRoot) {
        return editor;
    }

    if (isRoot && isEditorThatElevates) {
        if (contains_like_qt(elevate, "sudo")) {
            elevate += " -u $(logname)";
        } else {
            elevate += " --user $(logname)";
        }
    }

    if (isCliEditor) {
        return std::string("x-terminal-emulator -e ") + (isRoot ? elevate + " " : std::string()) + editor;
    }
    if (!isRoot) {
        return editor;
    }

    return elevate + " env DISPLAY=$DISPLAY XAUTHORITY=$XAUTHORITY " + editor;
}
