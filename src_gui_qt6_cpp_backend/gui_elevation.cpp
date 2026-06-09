#include "gui_elevation.h"

#include <QCoreApplication>
#include <QInputDialog>
#include <QLineEdit>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>

#include "command_runner.h"

namespace {

QMutex g_cacheMutex;
QString g_cachedPassword;

std::string askPasswordOnGuiThread()
{
    {
        QMutexLocker lock(&g_cacheMutex);
        if (!g_cachedPassword.isEmpty()) {
            return g_cachedPassword.toStdString();
        }
    }

    bool ok = false;
    const QString password = QInputDialog::getText(
        nullptr,
        QObject::tr("Administrator Access Required"),
        QObject::tr("This operation requires administrator privileges.\nPlease enter your password:"),
        QLineEdit::Password,
        QString(),
        &ok);
    if (!ok || password.isEmpty()) {
        return {};
    }

    {
        QMutexLocker lock(&g_cacheMutex);
        g_cachedPassword = password;
    }
    return password.toStdString();
}

std::string askPasswordForCommandRunner()
{
    QCoreApplication *app = QCoreApplication::instance();
    if (app == nullptr) {
        return {};
    }

    if (QThread::currentThread() == app->thread()) {
        return askPasswordOnGuiThread();
    }

    std::string result;
    QMetaObject::invokeMethod(
        app,
        [&result]() { result = askPasswordOnGuiThread(); },
        Qt::BlockingQueuedConnection);
    return result;
}

CommandRunner::GuiElevationHooks g_guiElevationHooks;

} // namespace

void GuiElevation::install()
{
    g_guiElevationHooks.askPassword = askPasswordForCommandRunner;
    g_guiElevationHooks.invalidateCachedPassword = []() { GuiElevation::clearCachedPassword(); };
    CommandRunner::setGuiElevationHooks(&g_guiElevationHooks);
}

void GuiElevation::clearCachedPassword()
{
    QMutexLocker lock(&g_cacheMutex);
    g_cachedPassword.clear();
}
