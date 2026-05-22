#include "command_line_parser_std.h"

#include "i18n_cli.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

namespace {
std::string replaceAll(std::string s, const std::string &from, const std::string &to)
{
    if (from.empty()) {
        return s;
    }
    std::size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::string leftJustified(const std::string &s, int width)
{
    if (width <= 0) {
        return std::string();
    }
    if (static_cast<int>(s.size()) >= width) {
        return s.substr(0, static_cast<std::size_t>(width));
    }
    std::string out = s;
    out.append(static_cast<std::size_t>(width) - out.size(), ' ');
    return out;
}

bool isSpaceAscii(char c)
{
    return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f' || c == '\v';
}
} // namespace

CommandLineParserStd::Option::Option(const std::vector<std::string> &names,
                                    const std::string &description,
                                    const std::string &valueName)
    : m_names(names)
    , m_description(description)
    , m_valueName(valueName)
{
}

CommandLineParserStd::CommandLineParserStd() = default;

void CommandLineParserStd::setApplicationName(const std::string &name)
{
    m_applicationName = name;
}

void CommandLineParserStd::setApplicationDescription(const std::string &description)
{
    m_description = description;
}

CommandLineParserStd::Option CommandLineParserStd::addHelpOption()
{
    Option opt({"h", "help"}, trQt("Displays help on commandline options."));
    addOption(opt);
    m_hasHelpOption = true;

    Option optHelpAll({"help-all"}, trQt("Displays help including Qt specific options."));
    addOption(optHelpAll);

    return opt;
}

CommandLineParserStd::Option CommandLineParserStd::addVersionOption()
{
    Option opt({"v", "version"}, trQt("Displays version information."));
    addOption(opt);
    m_hasVersionOption = true;
    return opt;
}

void CommandLineParserStd::addOption(const Option &option)
{
    m_options.push_back(option);
}

std::string CommandLineParserStd::stripLeadingDashes(const std::string &opt)
{
    std::size_t i = 0;
    while (i < opt.size() && opt[i] == '-') {
        ++i;
    }
    return opt.substr(i);
}

bool CommandLineParserStd::isLongOptionToken(const std::string &arg)
{
    return arg.size() > 2 && arg.rfind("--", 0) == 0;
}

bool CommandLineParserStd::isShortOptionToken(const std::string &arg)
{
    return arg.size() >= 2 && arg[0] == '-' && !(arg.size() >= 2 && arg.rfind("--", 0) == 0);
}

const CommandLineParserStd::Option *CommandLineParserStd::findOptionByName(const std::string &name) const
{
    const std::string key = stripLeadingDashes(name);
    for (const auto &opt : m_options) {
        for (const auto &n : opt.names()) {
            if (n == key) {
                return &opt;
            }
        }
    }
    return nullptr;
}

std::string CommandLineParserStd::trQt(const std::string &sourceText) const
{
    return I18nCli::tQt("QCommandLineParser", sourceText, "");
}

std::string CommandLineParserStd::formatArg1(const std::string &s, const std::string &arg1)
{
    return replaceAll(s, "%1", arg1);
}

std::string CommandLineParserStd::makeUnknownOptionsJoined(const std::vector<std::string> &unknown)
{
    std::string out;
    for (std::size_t i = 0; i < unknown.size(); ++i) {
        if (i) {
            out += ", ";
        }
        out += unknown[i];
    }
    return out;
}

bool CommandLineParserStd::parse(const std::vector<std::string> &arguments)
{
    m_errorText.clear();
    m_parsed.clear();
    m_unknownOptionNames.clear();

    for (std::size_t i = 1; i < arguments.size(); ++i) {
        const std::string &arg = arguments[i];
        if (arg == "--") {
            break;
        }

        if (isLongOptionToken(arg)) {
            std::string optName;
            std::string optValue;
            bool hasInlineValue = false;

            const std::size_t eq = arg.find('=');
            if (eq != std::string::npos) {
                optName = arg.substr(2, eq - 2);
                optValue = arg.substr(eq + 1);
                hasInlineValue = true;
            } else {
                optName = arg.substr(2);
            }

            const Option *opt = findOptionByName(optName);
            if (!opt) {
                m_unknownOptionNames.push_back(optName);
                continue;
            }

            auto canonicalNameOf = [](const Option &o) -> std::string {
                for (const auto &n : o.names()) {
                    if (n.size() > 1) {
                        return n;
                    }
                }
                if (!o.names().empty()) {
                    return o.names().front();
                }
                return std::string();
            };
            const std::string canonical = canonicalNameOf(*opt);

            const bool expectsValue = !opt->valueName().empty();
            if (expectsValue) {
                if (!hasInlineValue) {
                    if (i + 1 >= arguments.size()) {
                        m_errorText = formatArg1(trQt("Missing value after '%1'."), arg);
                        return false;
                    }
                    optValue = arguments[++i];
                }
                m_parsed.push_back({canonical, optValue, true});
            } else {
                if (hasInlineValue) {
                    m_errorText = formatArg1(trQt("Unexpected value after '%1'."), arg.substr(0, eq));
                    return false;
                }
                m_parsed.push_back({canonical, std::string(), false});
            }
            continue;
        }

        if (isShortOptionToken(arg) && arg != "-") {
            const std::string s = arg.substr(1);
            if (s.empty()) {
                continue;
            }

            const std::string shortName = s.substr(0, 1);
            const std::string attached = s.substr(1);

            const Option *opt = findOptionByName(shortName);
            if (!opt) {
                m_unknownOptionNames.push_back(shortName);
                continue;
            }

            auto canonicalNameOf = [](const Option &o) -> std::string {
                for (const auto &n : o.names()) {
                    if (n.size() > 1) {
                        return n;
                    }
                }
                if (!o.names().empty()) {
                    return o.names().front();
                }
                return std::string();
            };
            const std::string canonical = canonicalNameOf(*opt);

            const bool expectsValue = !opt->valueName().empty();
            if (!expectsValue) {
                if (!attached.empty()) {
                    m_errorText = std::string("option not expecting values: \"") + canonical + "\"";
                    return false;
                }
                m_parsed.push_back({canonical, std::string(), false});
                continue;
            }

            std::string val;
            if (!attached.empty()) {
                val = attached;
            } else {
                if (i + 1 >= arguments.size()) {
                    m_errorText = formatArg1(trQt("Missing value after '%1'."), arg);
                    return false;
                }
                val = arguments[++i];
            }

            m_parsed.push_back({canonical, val, true});
            continue;
        }

        // positional: ignore
    }

    if (!m_unknownOptionNames.empty()) {
        if (m_unknownOptionNames.size() == 1) {
            m_errorText = formatArg1(trQt("Unknown option '%1'."), m_unknownOptionNames.front());
            return false;
        }
        const std::string joined = makeUnknownOptionsJoined(m_unknownOptionNames);
        m_errorText = formatArg1(trQt("Unknown options: %1."), joined);
        return false;
    }

    return true;
}

bool CommandLineParserStd::isSet(const std::string &name) const
{
    const std::string key = stripLeadingDashes(name);
    const Option *opt = findOptionByName(key);
    const std::string canonical = opt ? (opt->names().empty() ? key : (std::find_if(opt->names().begin(), opt->names().end(), [](const std::string &n) { return n.size() > 1; }) != opt->names().end() ? *std::find_if(opt->names().begin(), opt->names().end(), [](const std::string &n) { return n.size() > 1; }) : opt->names().front())) : key;

    for (const auto &p : m_parsed) {
        if (p.name == canonical) {
            return true;
        }
    }
    return false;
}

std::string CommandLineParserStd::value(const std::string &name) const
{
    const std::string key = stripLeadingDashes(name);
    const Option *opt = findOptionByName(key);
    const std::string canonical = opt ? (opt->names().empty() ? key : (std::find_if(opt->names().begin(), opt->names().end(), [](const std::string &n) { return n.size() > 1; }) != opt->names().end() ? *std::find_if(opt->names().begin(), opt->names().end(), [](const std::string &n) { return n.size() > 1; }) : opt->names().front())) : key;

    for (const auto &p : m_parsed) {
        if (p.name == canonical && p.hasValue) {
            return p.value;
        }
    }
    return std::string();
}

std::vector<std::string> CommandLineParserStd::values(const std::string &name) const
{
    const std::string key = stripLeadingDashes(name);
    const Option *opt = findOptionByName(key);
    const std::string canonical = opt ? (opt->names().empty() ? key : (std::find_if(opt->names().begin(), opt->names().end(), [](const std::string &n) { return n.size() > 1; }) != opt->names().end() ? *std::find_if(opt->names().begin(), opt->names().end(), [](const std::string &n) { return n.size() > 1; }) : opt->names().front())) : key;

    std::vector<std::string> out;
    for (const auto &p : m_parsed) {
        if (p.name == canonical && p.hasValue) {
            out.push_back(p.value);
        }
    }
    return out;
}

std::string CommandLineParserStd::errorText() const
{
    return m_errorText;
}

std::string CommandLineParserStd::defaultHelpHeader()
{
    // Minimal header compatible with QCommandLineParser::helpText() output
    // produced by our oracle tests (only option blocks are compared there).
    // Keep empty here; helpText() composes the full output.
    return std::string();
}

std::string CommandLineParserStd::wrapTextLikeQt(const std::string &names,
                                                int optionNameMaxWidth,
                                                const std::string &description)
{
    const char nl = '\n';
    const std::string indentation = "  ";

    std::size_t nameIndex = 0;
    const auto nextNameSection = [&]() -> std::string {
        const std::string section = names.substr(nameIndex, static_cast<std::size_t>(optionNameMaxWidth));
        nameIndex += section.size();
        return section;
    };

    std::string text;
    std::size_t lineStart = 0;
    long long lastBreakable = -1;
    const int max = 79 - (static_cast<int>(indentation.size()) + optionNameMaxWidth + 1);
    int x = 0;
    const std::size_t len = description.size();

    for (std::size_t i = 0; i < len; ++i) {
        ++x;
        const char c = description[i];
        if (isSpaceAscii(c)) {
            lastBreakable = static_cast<long long>(i);
        }

        long long breakAt = -1;
        long long nextLineStart = -1;
        if (x > max && lastBreakable != -1) {
            breakAt = lastBreakable;
            nextLineStart = lastBreakable + 1;
        } else if ((x > max - 1 && lastBreakable == -1) || i == len - 1) {
            breakAt = static_cast<long long>(i + 1);
            nextLineStart = breakAt;
        } else if (c == nl) {
            breakAt = static_cast<long long>(i);
            nextLineStart = static_cast<long long>(i + 1);
        }

        if (breakAt != -1) {
            const std::size_t numChars = static_cast<std::size_t>(breakAt) - lineStart;
            text += indentation;
            text += leftJustified(nextNameSection(), optionNameMaxWidth);
            text.push_back(' ');
            text += description.substr(lineStart, numChars);
            text.push_back(nl);

            x = 0;
            lastBreakable = -1;
            lineStart = static_cast<std::size_t>(nextLineStart);
            if (lineStart < len && isSpaceAscii(description[lineStart])) {
                ++lineStart;
            }
            i = lineStart;
        }
    }

    while (nameIndex < names.size()) {
        text += indentation;
        text += nextNameSection();
        text.push_back(nl);
    }

    return text;
}

std::string CommandLineParserStd::helpText() const
{
    // Build help text to match the oracle's QCommandLineParser helpText output.
    // We intentionally focus on the formatting used by our unit tests.

    std::string out;

    out += "Usage: ";
    out += m_applicationName;
    out += " [options]\n";

    if (!m_description.empty()) {
        out += m_description;
        out += "\n";
    }
    out += "\n";

    out += "Options:\n";

    // Build option names string like Qt: "  -h, --help" etc.
    int longestOptionNameString = 0;
    struct OptLine {
        std::string names;
        std::string descr;
    };
    std::vector<OptLine> lines;
    lines.reserve(m_options.size());

    for (const auto &opt : m_options) {
        std::string names;
        for (std::size_t i = 0; i < opt.names().size(); ++i) {
            const std::string &n = opt.names()[i];
            if (i) {
                names += ", ";
            }
            if (n.size() == 1) {
                names += "-";
                names += n;
            } else {
                names += "--";
                names += n;
            }
        }

        if (!opt.valueName().empty()) {
            names += " <";
            names += opt.valueName();
            names += ">";
        }

        longestOptionNameString = std::max(longestOptionNameString, static_cast<int>(names.size()));
        lines.push_back({names, opt.description()});
    }

    // Qt: ++longestOptionNameString; optionNameMaxWidth = min(50, longestOptionNameString)
    ++longestOptionNameString;
    const int optionNameMaxWidth = std::min(50, longestOptionNameString);

    for (const auto &l : lines) {
        out += wrapTextLikeQt(l.names, optionNameMaxWidth, l.descr);
    }

    return out;
}

bool CommandLineParserStd::loadCliParserTranslations(const std::string &locale, const std::string &kvPath)
{
    return I18nCli::loadCliParserLocaleKv(locale, kvPath);
}
