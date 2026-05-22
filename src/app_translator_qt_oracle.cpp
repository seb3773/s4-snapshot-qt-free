#include "app_translator_qt_oracle.h"

#include <QCoreApplication>
#include <QTranslator>

namespace {

static QTranslator g_tr;

}

bool AppTranslatorQtOracle::loadFromDir(const QString &dir,
                                       const QString &baseName,
                                       const QString &localeName)
{
    QCoreApplication::removeTranslator(&g_tr);
    const bool ok = g_tr.load(baseName + "_" + localeName, dir);
    if (ok) {
        QCoreApplication::installTranslator(&g_tr);
    }
    return ok;
}

QString AppTranslatorQtOracle::translate(const QString &context,
                                        const QString &sourceText,
                                        const QString &comment,
                                        int n)
{
    const QString t = QCoreApplication::translate(
        context.toLocal8Bit().constData(),
        sourceText.toLocal8Bit().constData(),
        comment.isEmpty() ? nullptr : comment.toLocal8Bit().constData(),
        n);
    return t;
}
