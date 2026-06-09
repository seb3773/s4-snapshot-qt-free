#include "systeminfo_cpp.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <unistd.h>
#include <vector>

#include "command_runner.h"
#include "embedded/embedded_assets_runtime.h"
#include "process_runner.h"

namespace {

#ifdef UNIT_TESTS
static const SystemInfoCpp::Hooks *g_hooks = nullptr;
#endif

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

std::string getenv_non_empty(const char *name)
{
    const char *value = std::getenv(name);
    return (value && *value) ? std::string(value) : std::string();
}

std::string runtime_scripts_cache_dir()
{
    const std::string xdg_cache_home = getenv_non_empty("XDG_CACHE_HOME");
    if (!xdg_cache_home.empty() && xdg_cache_home.front() == '/') {
        return (std::filesystem::path(xdg_cache_home) / "s4-snapshot" / "runtime-scripts").string();
    }

    const std::string home = getenv_non_empty("HOME");
    if (!home.empty() && home.front() == '/') {
        return (std::filesystem::path(home) / ".cache" / "s4-snapshot" / "runtime-scripts").string();
    }

    return (std::filesystem::path("/tmp") / ("s4-snapshot-" + std::to_string(static_cast<long long>(::getuid())))
            / "runtime-scripts")
        .string();
}

std::string normalize_script_lines_to_spaces(std::string text)
{
    for (char &ch : text) {
        if (ch == '\n' || ch == '\r') {
            ch = ' ';
        }
    }
    return text;
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
    const std::string cacheDir = runtime_scripts_cache_dir();
    const EmbeddedAssetsRuntime::Result extract = EmbeddedAssetsRuntime::extractRuntimeScriptsTo(cacheDir);
    if (!extract.ok) {
        return std::string();
    }

    const ProcessRunner::Result result = ProcessRunner::run(
        (std::filesystem::path(cacheDir) / "snapshot-bootparameter.sh").string(), {});
    if (!result.started || result.exitStatus != ProcessRunner::ExitStatus::NormalExit || result.exitCode != 0) {
        return std::string();
    }

    return normalize_script_lines_to_spaces(result.stdoutText);
}
