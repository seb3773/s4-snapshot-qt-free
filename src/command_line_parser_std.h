#pragma once

#include <map>
#include <string>
#include <vector>

class CommandLineParserStd
{
public:
    class Option
    {
    public:
        Option() = default;
        Option(const std::vector<std::string> &names,
               const std::string &description,
               const std::string &valueName = std::string());

        [[nodiscard]] const std::vector<std::string> &names() const { return m_names; }
        [[nodiscard]] const std::string &description() const { return m_description; }
        [[nodiscard]] const std::string &valueName() const { return m_valueName; }

    private:
        std::vector<std::string> m_names;
        std::string m_description;
        std::string m_valueName;
    };

    CommandLineParserStd();

    void setApplicationName(const std::string &name);
    void setApplicationDescription(const std::string &description);

    Option addHelpOption();
    Option addVersionOption();

    void addOption(const Option &option);

    [[nodiscard]] bool parse(const std::vector<std::string> &arguments);

    [[nodiscard]] bool isSet(const std::string &name) const;
    [[nodiscard]] std::string value(const std::string &name) const;
    [[nodiscard]] std::vector<std::string> values(const std::string &name) const;

    [[nodiscard]] std::string errorText() const;
    [[nodiscard]] std::string helpText() const;

    [[nodiscard]] bool loadCliParserTranslations(const std::string &locale, const std::string &kvPath);

private:
    std::string m_applicationName;
    std::string m_description;

    std::vector<Option> m_options;
    bool m_hasHelpOption = false;
    bool m_hasVersionOption = false;

    std::string m_errorText;

    struct ParsedOptionValue {
        std::string name;
        std::string value;
        bool hasValue = false;
    };

    std::vector<ParsedOptionValue> m_parsed;
    std::vector<std::string> m_unknownOptionNames;

    [[nodiscard]] const Option *findOptionByName(const std::string &name) const;

    [[nodiscard]] std::string trQt(const std::string &sourceText) const;
    [[nodiscard]] static std::string formatArg1(const std::string &s, const std::string &arg1);

    [[nodiscard]] static std::string stripLeadingDashes(const std::string &opt);
    [[nodiscard]] static bool isLongOptionToken(const std::string &arg);
    [[nodiscard]] static bool isShortOptionToken(const std::string &arg);
    [[nodiscard]] static std::string makeUnknownOptionsJoined(const std::vector<std::string> &unknown);

    [[nodiscard]] static std::string wrapTextLikeQt(const std::string &names,
                                                    int optionNameMaxWidth,
                                                    const std::string &description);

    [[nodiscard]] static std::string defaultHelpHeader();
};
