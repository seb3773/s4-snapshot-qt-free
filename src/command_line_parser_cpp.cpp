#include "command_line_parser_cpp.h"

#include "i18n_cli.h"

#include <QCoreApplication>

namespace {

static bool isLongOptionToken(const QString &arg)
{
    return arg.size() > 2 && arg.startsWith("--");
}

static bool isShortOptionToken(const QString &arg)
{
    return arg.size() >= 2 && arg.startsWith('-') && !arg.startsWith("--");
}

static QString stripLeadingDashes(const QString &opt)
{
    QString s = opt;
    while (s.startsWith('-')) {
        s.remove(0, 1);
    }
    return s;
}

static QStringList makeUnknownOptionsList(const QStringList &unknown)
{
    QStringList out;
    out.reserve(unknown.size());
    for (const auto &u : unknown) {
        out.push_back(u);
    }
    return out;
}

static QString qtWrapText(const QString &names, int optionNameMaxWidth, const QString &description)
{
    const QChar nl('\n');
    const QString indentation = QStringLiteral("  ");

    int nameIndex = 0;
    const auto nextNameSection = [&]() {
        QString section = names.mid(nameIndex, optionNameMaxWidth);
        nameIndex += section.size();
        return section;
    };

    QString text;
    qsizetype lineStart = 0;
    qsizetype lastBreakable = -1;
    const int max = 79 - (indentation.size() + optionNameMaxWidth + 1);
    int x = 0;
    const qsizetype len = description.size();

    for (qsizetype i = 0; i < len; ++i) {
        ++x;
        const QChar c = description.at(i);
        if (c.isSpace()) {
            lastBreakable = i;
        }

        qsizetype breakAt = -1;
        qsizetype nextLineStart = -1;
        if (x > max && lastBreakable != -1) {
            breakAt = lastBreakable;
            nextLineStart = lastBreakable + 1;
        } else if ((x > max - 1 && lastBreakable == -1) || i == len - 1) {
            breakAt = i + 1;
            nextLineStart = breakAt;
        } else if (c == nl) {
            breakAt = i;
            nextLineStart = i + 1;
        }

        if (breakAt != -1) {
            const qsizetype numChars = breakAt - lineStart;
            text += indentation + nextNameSection().leftJustified(optionNameMaxWidth) + QChar(' ');
            text += QStringView(description).mid(lineStart, numChars).toString() + nl;
            x = 0;
            lastBreakable = -1;
            lineStart = nextLineStart;
            if (lineStart < len && description.at(lineStart).isSpace()) {
                ++lineStart;
            }
            i = lineStart;
        }
    }

    while (nameIndex < names.size()) {
        text += indentation + nextNameSection() + nl;
    }

    return text;
}

} // namespace

CommandLineParserCpp::Option::Option(const QStringList &names, const QString &description, const QString &valueName)
    : m_names(names),
      m_description(description),
      m_valueName(valueName)
{
}

CommandLineParserCpp::CommandLineParserCpp() = default;

void CommandLineParserCpp::setApplicationDescription(const QString &description)
{
    m_description = description;
}

CommandLineParserCpp::Option CommandLineParserCpp::addHelpOption()
{
    Option opt({QStringLiteral("h"), QStringLiteral("help")}, trQt(QStringLiteral("Displays help on commandline options.")));
    addOption(opt);
    m_hasHelpOption = true;

    Option optHelpAll({QStringLiteral("help-all")}, trQt(QStringLiteral("Displays help, including generic Qt options.")));
    addOption(optHelpAll);

    return opt;
}

CommandLineParserCpp::Option CommandLineParserCpp::addVersionOption()
{
    Option opt({QStringLiteral("v"), QStringLiteral("version")}, trQt(QStringLiteral("Displays version information.")));
    addOption(opt);
    m_hasVersionOption = true;
    return opt;
}

void CommandLineParserCpp::addOption(const Option &option)
{
    m_options.push_back(option);
}

const CommandLineParserCpp::Option *CommandLineParserCpp::findOptionByName(const QString &name) const
{
    const QString key = stripLeadingDashes(name);
    for (const auto &opt : m_options) {
        for (const auto &n : opt.names()) {
            if (n == key) {
                return &opt;
            }
        }
    }
    return nullptr;
}

QString CommandLineParserCpp::trQt(const QString &sourceText) const
{
    return QString::fromStdString(I18nCli::tQt("QCommandLineParser", sourceText.toStdString(), ""));
}

QString CommandLineParserCpp::formatArg1(const QString &s, const QString &arg1) const
{
    QString out = s;
    out.replace(QStringLiteral("%1"), arg1);
    return out;
}

bool CommandLineParserCpp::parse(const QStringList &arguments)
{
    m_errorText.clear();
    m_parsed.clear();
    m_unknownOptionNames.clear();

    for (int i = 1; i < arguments.size(); ++i) {
        const QString arg = arguments.at(i);
        if (arg == QStringLiteral("--")) {
            break;
        }

        if (isLongOptionToken(arg)) {
            QString optName;
            QString optValue;
            bool hasInlineValue = false;

            const int eq = arg.indexOf('=');
            if (eq >= 0) {
                optName = arg.mid(2, eq - 2);
                optValue = arg.mid(eq + 1);
                hasInlineValue = true;
            } else {
                optName = arg.mid(2);
            }

            const Option *opt = findOptionByName(optName);
            if (!opt) {
                m_unknownOptionNames.push_back(optName);
                continue;
            }

            const bool expectsValue = !opt->valueName().isEmpty();
            if (expectsValue) {
                if (!hasInlineValue) {
                    if (i + 1 >= arguments.size()) {
                        m_errorText = formatArg1(trQt(QStringLiteral("Missing value after '%1'.")), arg);
                        return false;
                    }
                    optValue = arguments.at(++i);
                }

                m_parsed.push_back({optName, optValue, true});
            } else {
                if (hasInlineValue) {
                    m_errorText = formatArg1(trQt(QStringLiteral("Unexpected value after '%1'.")), arg.left(eq));
                    return false;
                }
                m_parsed.push_back({optName, QString(), false});
            }
            continue;
        }

        if (isShortOptionToken(arg) && arg != QStringLiteral("-")) {
            QString s = arg.mid(1);
            for (int k = 0; k < s.size(); ++k) {
                const QString shortName = s.mid(k, 1);
                const Option *opt = findOptionByName(shortName);
                if (!opt) {
                    m_unknownOptionNames.push_back(shortName);
                    continue;
                }

                const bool expectsValue = !opt->valueName().isEmpty();
                if (!expectsValue) {
                    m_parsed.push_back({shortName, QString(), false});
                    continue;
                }

                QString val;
                const bool hasRest = (k + 1 < s.size());
                if (hasRest) {
                    val = s.mid(k + 1);
                    k = s.size();
                } else {
                    if (i + 1 >= arguments.size()) {
                        m_errorText = formatArg1(trQt(QStringLiteral("Missing value after '%1'.")), arg);
                        return false;
                    }
                    val = arguments.at(++i);
                }

                m_parsed.push_back({shortName, val, true});
                break;
            }
            continue;
        }

        // positional: ignore for now
    }

    if (!m_unknownOptionNames.isEmpty()) {
        if (m_unknownOptionNames.size() == 1) {
            m_errorText = formatArg1(trQt(QStringLiteral("Unknown option '%1'.")), m_unknownOptionNames.constFirst());
            return false;
        }
        const QString joined = makeUnknownOptionsList(m_unknownOptionNames).join(QStringLiteral(", "));
        m_errorText = formatArg1(trQt(QStringLiteral("Unknown options: %1.")), joined);
        return false;
    }

    return true;
}

bool CommandLineParserCpp::isSet(const QString &name) const
{
    const QString key = stripLeadingDashes(name);
    for (const auto &p : m_parsed) {
        if (p.name == key) {
            return true;
        }
    }

    // Also match by synonyms
    const Option *opt = findOptionByName(key);
    if (!opt) {
        return false;
    }
    for (const auto &p : m_parsed) {
        for (const auto &n : opt->names()) {
            if (p.name == n) {
                return true;
            }
        }
    }

    return false;
}

QString CommandLineParserCpp::value(const QString &name) const
{
    const QString key = stripLeadingDashes(name);
    const Option *opt = findOptionByName(key);
    if (!opt) {
        return {};
    }

    for (int i = m_parsed.size() - 1; i >= 0; --i) {
        const auto &p = m_parsed.at(i);
        for (const auto &n : opt->names()) {
            if (p.name == n && p.hasValue) {
                return p.value;
            }
        }
    }

    return {};
}

QStringList CommandLineParserCpp::values(const QString &name) const
{
    const QString key = stripLeadingDashes(name);
    const Option *opt = findOptionByName(key);
    if (!opt) {
        return {};
    }

    QStringList out;
    for (const auto &p : m_parsed) {
        for (const auto &n : opt->names()) {
            if (p.name == n && p.hasValue) {
                out.push_back(p.value);
            }
        }
    }
    return out;
}

QString CommandLineParserCpp::errorText() const
{
    return m_errorText;
}

QString CommandLineParserCpp::helpText() const
{
    const QChar nl('\n');
    QString text;
    QString usage;

    const QString exeName = !QCoreApplication::arguments().isEmpty()
        ? QCoreApplication::arguments().constFirst()
        : QStringLiteral("<executable_name>");

    usage += exeName;
    if (!m_options.isEmpty()) {
        usage += QChar(' ') + trQt(QStringLiteral("[options]"));
    }

    text += formatArg1(trQt(QStringLiteral("Usage: %1")), usage) + nl;
    if (!m_description.isEmpty()) {
        text += m_description + nl;
    }
    text += nl;

    if (!m_options.isEmpty()) {
        text += trQt(QStringLiteral("Options:")) + nl;
    }

    QStringList optionNameList;
    optionNameList.reserve(m_options.size());
    qsizetype longest = 0;

    for (const auto &option : m_options) {
        QString optionNamesString;
        for (const auto &n : option.names()) {
            const int numDashes = (n.size() == 1) ? 1 : 2;
            optionNamesString += (numDashes == 1 ? QStringLiteral("-") : QStringLiteral("--")) + n + QStringLiteral(", ");
        }
        if (!option.names().isEmpty()) {
            optionNamesString.chop(2);
        }
        if (!option.valueName().isEmpty()) {
            optionNamesString += QStringLiteral(" <") + option.valueName() + QChar('>');
        }
        optionNameList.push_back(optionNamesString);
        longest = qMax(longest, optionNamesString.size());
    }

    ++longest;
    const int optionNameMaxWidth = qMin(50, int(longest));

    for (int i = 0; i < m_options.size(); ++i) {
        text += qtWrapText(optionNameList.at(i), optionNameMaxWidth, m_options.at(i).description());
    }

    return text;
}

bool CommandLineParserCpp::loadCliParserTranslations(const QString &locale, const QString &kvPath)
{
    return I18nCli::loadCliParserLocaleKv(locale.toStdString(), kvPath.toStdString())
        && I18nCli::setLocale(locale.toStdString());
}
