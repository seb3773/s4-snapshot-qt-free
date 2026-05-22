#pragma once

#include <string>

namespace I18nCli
{
struct QtKeyParts {
    std::string context;
    std::string sourceText;
    std::string comment;
};

[[nodiscard]] std::string makeQtKey(const std::string &context,
                                   const std::string &sourceText,
                                   const std::string &comment);

[[nodiscard]] QtKeyParts parseQtKey(const std::string &key);

[[nodiscard]] bool loadCliParserLocaleKv(const std::string &locale,
                                        const std::string &kvFilePath);

[[nodiscard]] bool setLocale(const std::string &locale);

[[nodiscard]] std::string tQt(const std::string &context,
                             const std::string &sourceText,
                             const std::string &comment);
}
