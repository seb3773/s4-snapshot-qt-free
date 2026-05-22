#pragma once

#include <QString>

class AppTranslatorQtOracle
{
public:
    [[nodiscard]] static bool loadFromDir(const QString &dir,
                                         const QString &baseName,
                                         const QString &localeName);

    [[nodiscard]] static QString translate(const QString &context,
                                          const QString &sourceText,
                                          const QString &comment = QString(),
                                          int n = -1);
};
