#include "embedded_assets_runtime.h"

#include "embedded_assets.h"
#include "settings_cpp.h"

#include <csignal>
#include <cstring>
#include <filesystem>

namespace {

constexpr std::size_t kSignalCleanupPathMax = 4095;

volatile sig_atomic_t g_signal_stop_requested = 0;
char g_signal_cleanup_work_dir[kSignalCleanupPathMax + 1] = {};

void handle_embedded_cleanup_signal(int /*signum*/)
{
    g_signal_stop_requested = 1;
}

} // namespace

std::string EmbeddedAssetsRuntime::embeddedRoot(const std::string &workDir)
{
    return (std::filesystem::path(workDir) / "_embedded").string();
}

std::string EmbeddedAssetsRuntime::liveFilesDir(const std::string &workDir)
{
    return (std::filesystem::path(embeddedRoot(workDir)) / "live-files").string();
}

std::string EmbeddedAssetsRuntime::runtimeScriptsDir(const std::string &workDir)
{
    return (std::filesystem::path(embeddedRoot(workDir)) / "scripts").string();
}

std::string EmbeddedAssetsRuntime::initrdBuildDir(const std::string &workDir)
{
    return (std::filesystem::path(embeddedRoot(workDir)) / "initrd-build").string();
}

std::string EmbeddedAssetsRuntime::initrdScriptsDir(const std::string &initrdBuildDir)
{
    return (std::filesystem::path(initrdBuildDir) / "share" / "s4-snapshot" / "scripts").string();
}

EmbeddedAssetsRuntime::Result EmbeddedAssetsRuntime::extractRuntimeScriptsTo(const std::string &destRoot)
{
    Result result;
    const EmbeddedAssets::Result extract = EmbeddedAssets::extractRuntimeScripts(destRoot);
    if (!extract.ok) {
        result.error = std::string("failed to extract embedded runtime scripts: ") + extract.error;
        return result;
    }
    result.ok = true;
    return result;
}

bool EmbeddedAssetsRuntime::signalStopRequested()
{
    return g_signal_stop_requested != 0;
}

void EmbeddedAssetsRuntime::requestStop()
{
    g_signal_stop_requested = 1;
}

std::string EmbeddedAssetsRuntime::signalStopWorkDir()
{
    return std::string(g_signal_cleanup_work_dir);
}

EmbeddedAssetsRuntime::Result EmbeddedAssetsRuntime::prepareWorkspace(SettingsCpp &settings,
                                                                      const Callbacks &cb)
{
    Result result;
    if (settings.workDir.empty()) {
        result.error = "work directory is empty";
        if (cb.critical) {
            cb.critical(result.error);
        }
        return result;
    }

    std::error_code ec;
    std::filesystem::create_directories(embeddedRoot(settings.workDir), ec);
    if (ec) {
        result.error = std::string("cannot create embedded workspace: ") + ec.message();
        if (cb.critical) {
            cb.critical(result.error);
        }
        return result;
    }

    installSignalCleanup(settings.workDir);

    {
        const std::string dest = liveFilesDir(settings.workDir);
        const EmbeddedAssets::Result extract = EmbeddedAssets::extractLiveFiles(dest);
        if (!extract.ok) {
            result.error = std::string("failed to extract embedded live-files: ") + extract.error;
            if (cb.critical) {
                cb.critical(result.error);
            }
            return result;
        }
        settings.dataFilesPath = dest;
    }

    {
        const std::string dest = runtimeScriptsDir(settings.workDir);
        result = extractRuntimeScriptsTo(dest);
        if (!result.ok) {
            if (cb.critical) {
                cb.critical(result.error);
            }
            return result;
        }
        settings.runtimeScriptsPath = dest;
    }

    std::filesystem::create_directories(initrdBuildDir(settings.workDir), ec);
    if (ec) {
        result.error = std::string("cannot create initrd build directory: ") + ec.message();
        if (cb.critical) {
            cb.critical(result.error);
        }
        return result;
    }

    result.ok = true;
    return result;
}

void EmbeddedAssetsRuntime::installSignalCleanup(const std::string &workDir)
{
    std::strncpy(g_signal_cleanup_work_dir, workDir.c_str(), kSignalCleanupPathMax);
    g_signal_cleanup_work_dir[kSignalCleanupPathMax] = '\0';
    g_signal_stop_requested = 0;
    std::signal(SIGINT, handle_embedded_cleanup_signal);
    std::signal(SIGTERM, handle_embedded_cleanup_signal);
}

void EmbeddedAssetsRuntime::clearSignalCleanup()
{
    g_signal_cleanup_work_dir[0] = '\0';
    g_signal_stop_requested = 0;
    std::signal(SIGINT, SIG_DFL);
    std::signal(SIGTERM, SIG_DFL);
}

void EmbeddedAssetsRuntime::cleanupEmbeddedTree(const std::string &workDir)
{
    if (workDir.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::remove_all(embeddedRoot(workDir), ec);
}

bool EmbeddedAssetsRuntime::handleSignalStopIfRequested()
{
    if (!signalStopRequested()) {
        return false;
    }
    const std::string workDir = signalStopWorkDir();
    if (!workDir.empty()) {
        cleanupEmbeddedTree(workDir);
    }
    return true;
}
