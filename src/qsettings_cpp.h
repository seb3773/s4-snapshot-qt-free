#pragma once

#include <map>
#include <functional>
#include <string>
#include <vector>

class QSettingsCpp
{
public:
#ifdef UNIT_TESTS
    struct Hooks {
        std::function<std::string(const std::string &filePath,
                                  const std::string &key,
                                  const std::string &defaultValue)>
            nativeGeneralValueString;
    };

    static void setHooksForTests(const Hooks *hooks);
#endif

    [[nodiscard]] static std::string nativeGeneralValueString(const std::string &filePath,
                                                             const std::string &key,
                                                             const std::string &defaultValue);

    [[nodiscard]] static std::string iniGeneralValueString(const std::string &filePath,
                                                           const std::string &key,
                                                           const std::string &defaultValue);

    [[nodiscard]] static bool nativeUserContainsKey(const std::string &organization,
                                                    const std::string &application,
                                                    const std::string &key);

    [[nodiscard]] static std::string nativeUserValueString(const std::string &organization,
                                                           const std::string &application,
                                                           const std::string &key,
                                                           const std::string &defaultValue);

    [[nodiscard]] static bool nativeUserContainsKeyFromBaseDir(const std::string &configDir,
                                                               const std::string &organization,
                                                               const std::string &application,
                                                               const std::string &key);

    [[nodiscard]] static std::string nativeUserValueStringFromBaseDir(const std::string &configDir,
                                                                      const std::string &organization,
                                                                      const std::string &application,
                                                                      const std::string &key,
                                                                      const std::string &defaultValue);

    [[nodiscard]] static std::string nativeUserPrimaryFilePathFromBaseDir(const std::string &configDir,
                                                                          const std::string &organization,
                                                                          const std::string &application);

    [[nodiscard]] static std::vector<std::string> nativeGeneralAllKeys(const std::string &filePath);

    [[nodiscard]] static std::map<std::string, std::string> nativeGeneralAllKeyValues(const std::string &filePath);

    [[nodiscard]] static bool nativeGeneralSetValueString(const std::string &filePath,
                                                          const std::string &key,
                                                          const std::string &value);

    [[nodiscard]] static bool variantStringToBoolLikeQt(const std::string &s);
};
