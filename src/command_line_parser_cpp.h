#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

class CommandLineParserCpp
{
public:
    class Option
    {
    public:
        Option() = default;
        Option(const QStringList &names, const QString &description, const QString &valueName = QString());

        [[nodiscard]] const QStringList &names() const { return m_names; }
        [[nodiscard]] const QString &description() const { return m_description; }
        [[nodiscard]] const QString &valueName() const { return m_valueName; }

    private:
        QStringList m_names;
        QString m_description;
        QString m_valueName;
    };

    CommandLineParserCpp();

    void setApplicationDescription(const QString &description);

    Option addHelpOption();
    Option addVersionOption();

    void addOption(const Option &option);

    [[nodiscard]] bool parse(const QStringList &arguments);

    [[nodiscard]] bool isSet(const QString &name) const;
    [[nodiscard]] QString value(const QString &name) const;
    [[nodiscard]] QStringList values(const QString &name) const;

    [[nodiscard]] QString errorText() const;
    [[nodiscard]] QString helpText() const;

    [[nodiscard]] bool loadCliParserTranslations(const QString &locale, const QString &kvPath);

private:
    QString m_description;

    QVector<Option> m_options;
    bool m_hasHelpOption = false;
    bool m_hasVersionOption = false;

    QString m_errorText;

    struct ParsedOptionValue {
        QString name;
        QString value;
        bool hasValue = false;
    };

    QVector<ParsedOptionValue> m_parsed;
    QStringList m_unknownOptionNames;

    [[nodiscard]] const Option *findOptionByName(const QString &name) const;

    [[nodiscard]] QString trQt(const QString &sourceText) const;
    [[nodiscard]] QString formatArg1(const QString &s, const QString &arg1) const;
};
