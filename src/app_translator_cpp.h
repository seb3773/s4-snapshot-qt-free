#pragma once

#include <optional>
#include <string>

class AppTranslatorCpp
{
public:
    [[nodiscard]] static bool loadFromDir(const std::string &dir,
                                         const std::string &baseName,
                                         const std::string &localeName);

    [[nodiscard]] static std::optional<std::string> translate(const std::string &context,
                                                             const std::string &sourceText,
                                                             const std::string &comment = std::string(),
                                                             int n = -1);

    [[nodiscard]] static std::string tQt(const std::string &context,
                                        const std::string &sourceText,
                                        const std::string &comment = std::string(),
                                        int n = -1);
};
