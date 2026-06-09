#pragma once

#include <string>

namespace S4SnapshotConfig {

constexpr const char kOrganizationName[] = "s4-snapshot";
constexpr const char kLegacyOrganizationName[] = "MX-Linux";
constexpr const char kShareDir[] = "/usr/share/s4-snapshot";

[[nodiscard]] std::string bundledConfigPath(const std::string &appName);
[[nodiscard]] std::string bundledExcludesPath(const std::string &appName);
[[nodiscard]] std::string legacyBundledExcludesPath(const std::string &appName);

[[nodiscard]] std::string userConfigBaseDirStd();
[[nodiscard]] std::string userConfigDirFromBase(const std::string &configBaseDir, const std::string &organizationName);
[[nodiscard]] std::string userExcludesPathFromBase(const std::string &configBaseDir,
                                                   const std::string &organizationName,
                                                   const std::string &appName);

// Ensures ~/.config/s4-snapshot/<app>.conf exists (migrate legacy MX-Linux file or seed from bundled defaults).
void ensureUserConfigFile(const std::string &configBaseDir,
                          const std::string &organizationName,
                          const std::string &appName,
                          const std::string &defaultsConfigPath);

// Ensures ~/.config/s4-snapshot/<app>-exclude.list exists when possible.
void ensureUserExcludesFile(const std::string &configBaseDir,
                            const std::string &organizationName,
                            const std::string &appName,
                            const std::string &defaultsExcludesPath);

// Copies a legacy configured excludes path into the canonical user file when needed,
// then returns the canonical path (always the active excludes file for this app).
[[nodiscard]] std::string normalizeUserExcludesToCanonical(const std::string &configBaseDir,
                                                             const std::string &organizationName,
                                                             const std::string &appName,
                                                             const std::string &configuredExcludesPath);

void persistSnapshotDir(const std::string &configBaseDir,
                        const std::string &organizationName,
                        const std::string &appName,
                        const std::string &snapshotDir);

void persistWorkDir(const std::string &configBaseDir,
                    const std::string &organizationName,
                    const std::string &appName,
                    const std::string &workDir);

} // namespace S4SnapshotConfig
