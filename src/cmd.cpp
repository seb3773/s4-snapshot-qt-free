#include "cmd.h"

#include <QCoreApplication>
#include <QDebug>
#include <QStringList>

#ifndef UNIT_TESTS
#error "src/cmd.cpp is Qt legacy oracle-only and must only be compiled in unit_tests (UNIT_TESTS=1)."
#endif

#include <unistd.h>

#include "file_cpp.h"
#include "embedded/embedded_helper_runtime.h"
#ifdef CLI_BUILD
#include "messagehandler_cpp.h"
#else
#include "messagehandler.h"
#endif

#ifdef CLI_BUILD

#include "command_runner.h"

Cmd::Cmd(QObject * /*parent*/)
    : elevationToolPath {Cmd::elevationTool()},
      helperPath {}
{
}

QString Cmd::elevationTool()
{
    return QString::fromStdString(CommandRunner::elevationTool());
}

bool Cmd::isCliMode()
{
    return true;
}

QString Cmd::loggedInUserName()
{
    return QString::fromStdString(CommandRunner::loggedInUserName());
}

QStringList Cmd::helperExecArgs(const QString &cmd, const QStringList &args)
{
    QStringList helperArgs {"exec", cmd};
    helperArgs += args;
    return helperArgs;
}

QString Cmd::getOut(const QString &cmd, QuietMode quiet)
{
    QString output;
    run(cmd, quiet);
    output = outBuffer;
    return output.trimmed();
}

QString Cmd::getOutAsRoot(const QString &cmd, const QStringList &args, QuietMode quiet)
{
    QString output;
    procAsRoot(cmd, args, &output, nullptr, quiet);
    return output;
}

bool Cmd::helperProc(const QStringList &helperArgs, QString *output, const QByteArray *input, QuietMode quiet)
{
    if (getuid() != 0 && elevationToolPath.isEmpty()) {
        const QString message = QObject::tr("No elevation tool found (sudo/doas/gksu).");
        qWarning().noquote() << message;
        return false;
    }

    const EmbeddedHelperRuntime::Result helper = EmbeddedHelperRuntime::ensureHelperAvailable();
    if (!helper.ok) {
        qWarning().noquote() << QString::fromStdString(helper.error);
        return false;
    }

    const QString helperPath = QString::fromStdString(helper.path);
    const QString program = (getuid() == 0) ? helperPath : elevationToolPath;
    QStringList programArgs = helperArgs;
    if (getuid() != 0) {
        programArgs.prepend(helperPath);
    }

    const bool result = proc(program, programArgs, output, input, quiet, Elevation::No);
    if (lastExitCode == EXIT_CODE_PERMISSION_DENIED || lastExitCode == EXIT_CODE_COMMAND_NOT_FOUND) {
        handleElevationError();
    }
    return result;
}

bool Cmd::proc(const QString &cmd, const QStringList &args, QString *output, const QByteArray *input, QuietMode quiet,
               Elevation elevation)
{
    if (elevation == Elevation::Yes) {
        return helperProc(helperExecArgs(cmd, args), output, input, quiet);
    }

    outBuffer.clear();
    if (quiet == QuietMode::No) {
        qDebug() << cmd << args;
    }

    std::string stdinText;
    if (input && !input->isEmpty()) {
        stdinText.assign(input->constData(), static_cast<size_t>(input->size()));
    }

    CommandRunner::Result r = CommandRunner::proc(cmd.toStdString(), [&]() {
        std::vector<std::string> out;
        out.reserve(static_cast<size_t>(args.size()));
        for (const QString &a : args) {
            out.push_back(a.toStdString());
        }
        return out;
    }(), stdinText, quiet == QuietMode::Yes ? CommandRunner::QuietMode::Yes : CommandRunner::QuietMode::No,
                                                 CommandRunner::Elevation::No);

    outBuffer = QString::fromStdString(r.mergedText).trimmed();
    lastExitCode = r.exitCode;
    lastNormalExit = r.normalExit;

    if (output) {
        *output = outBuffer;
    }
    return (r.normalExit && r.exitCode == 0);
}

bool Cmd::procAsRoot(const QString &cmd, const QStringList &args, QString *output, const QByteArray *input,
                     QuietMode quiet)
{
    return proc(cmd, args, output, input, quiet, Elevation::Yes);
}

bool Cmd::run(const QString &cmd, QuietMode quiet)
{
    return proc("/bin/bash", {"-c", cmd}, nullptr, nullptr, quiet);
}

QString Cmd::readAllOutput()
{
    return outBuffer.trimmed();
}

void Cmd::handleElevationError()
{
#ifdef CLI_BUILD
    MessageHandlerCpp::showMessage(
        MessageHandlerCpp::Critical,
        QObject::tr("Administrator Access Required").toStdString(),
        QObject::tr("This operation requires administrator privileges. Please restart the "
                    "application and enter your password when prompted.")
            .toStdString());
#else
    MessageHandler::showMessage(MessageHandler::Critical, QObject::tr("Administrator Access Required"),
                                QObject::tr("This operation requires administrator privileges. Please restart the "
                                            "application and enter your password when prompted."));
#endif
    exit(EXIT_FAILURE);
}

#else

#include <QEventLoop>
#include <QTimer>

Cmd::Cmd(QObject *parent)
    : QProcess(parent),
      elevationToolPath {Cmd::elevationTool()},
      helperPath {}
{
    connect(this, &Cmd::readyReadStandardOutput,
            [this] { emit outputAvailable(QString::fromLocal8Bit(readAllStandardOutput())); });
    connect(this, &Cmd::readyReadStandardError,
            [this] { emit errorAvailable(QString::fromLocal8Bit(readAllStandardError())); });
    connect(this, &Cmd::outputAvailable, [this](const QString &out) { outBuffer += out; });
    connect(this, &Cmd::errorAvailable, [this](const QString &out) { outBuffer += out; });
}

QString Cmd::elevationTool()
{
    const bool cli = Cmd::isCliMode();
    if (cli) {
        if (FileCpp::exists("/usr/bin/sudo")) return QStringLiteral("/usr/bin/sudo");
        if (FileCpp::exists("/usr/bin/doas")) return QStringLiteral("/usr/bin/doas");
        if (FileCpp::exists("/usr/bin/gksu")) return QStringLiteral("/usr/bin/gksu");
        return {};
    }
    if (FileCpp::exists("/usr/bin/sudo")) return QStringLiteral("/usr/bin/sudo");
    if (FileCpp::exists("/usr/bin/doas")) return QStringLiteral("/usr/bin/doas");
    if (FileCpp::exists("/usr/bin/gksu")) return QStringLiteral("/usr/bin/gksu");
    return {};
}

bool Cmd::isCliMode()
{
#ifdef CLI_BUILD
    return true;
#else
    const auto args = QCoreApplication::arguments();
    const bool forceCliMode = args.contains("--cli") || args.contains("-c") ||
                              args.contains("--help") || args.contains("-h") ||
                              QCoreApplication::applicationFilePath().contains("cli") ||
                              !qgetenv("MX_SNAPSHOT_CLI").isEmpty();
    const bool noWindowSystem = qgetenv("DISPLAY").isEmpty() && qgetenv("WAYLAND_DISPLAY").isEmpty();
    const QString qpa = QString::fromLocal8Bit(qgetenv("QT_QPA_PLATFORM"));
    const bool headlessQpa = (qpa == QLatin1String("offscreen") ||
                              qpa == QLatin1String("minimal") ||
                              qpa == QLatin1String("linuxfb"));
    return forceCliMode || noWindowSystem || headlessQpa;
#endif
}

QString Cmd::loggedInUserName()
{
    QString username = qEnvironmentVariable("SUDO_USER");
    if (username.isEmpty()) {
        username = qEnvironmentVariable("LOGNAME");
    }
    if (username.isEmpty()) {
        username = qEnvironmentVariable("USER");
    }
    if (username.isEmpty() || username == QLatin1String("root")) {
        username = Cmd().getOut("logname", Cmd::QuietMode::Yes).trimmed();
    }
    return username == QLatin1String("root") ? QString() : username;
}

QStringList Cmd::helperExecArgs(const QString &cmd, const QStringList &args)
{
    QStringList helperArgs {"exec", cmd};
    helperArgs += args;
    return helperArgs;
}

QString Cmd::getOut(const QString &cmd, QuietMode quiet)
{
    QString output;
    run(cmd, quiet);
    output = outBuffer;
    return output.trimmed();
}

QString Cmd::getOutAsRoot(const QString &cmd, const QStringList &args, QuietMode quiet)
{
    QString output;
    procAsRoot(cmd, args, &output, nullptr, quiet);
    return output;
}

bool Cmd::helperProc(const QStringList &helperArgs, QString *output, const QByteArray *input, QuietMode quiet)
{
    if (getuid() != 0 && elevationToolPath.isEmpty()) {
        const QString message = tr("No elevation tool found (sudo/doas/gksu).");
        qWarning().noquote() << message;
        emit errorAvailable(message);
        emit done();
        return false;
    }

    const EmbeddedHelperRuntime::Result helper = EmbeddedHelperRuntime::ensureHelperAvailable();
    if (!helper.ok) {
        const QString message = QString::fromStdString(helper.error);
        qWarning().noquote() << message;
        emit errorAvailable(message);
        emit done();
        return false;
    }

    const QString helperPath = QString::fromStdString(helper.path);
    const QString program = (getuid() == 0) ? helperPath : elevationToolPath;
    QStringList programArgs = helperArgs;
    if (getuid() != 0) {
        programArgs.prepend(helperPath);
    }

    const bool result = proc(program, programArgs, output, input, quiet, Elevation::No);
    if (exitCode() == EXIT_CODE_PERMISSION_DENIED || exitCode() == EXIT_CODE_COMMAND_NOT_FOUND) {
        handleElevationError();
    }
    return result;
}

bool Cmd::proc(const QString &cmd, const QStringList &args, QString *output, const QByteArray *input, QuietMode quiet,
               Elevation elevation)
{
    if (elevation == Elevation::Yes) {
        return helperProc(helperExecArgs(cmd, args), output, input, quiet);
    }

    outBuffer.clear();
    if (state() != QProcess::NotRunning) {
        qDebug() << "Process already running:" << program() << arguments();
        return false;
    }
    if (quiet == QuietMode::No) {
        qDebug() << cmd << args;
    }

    QEventLoop loop;
    connect(this, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), &loop, &QEventLoop::quit);
    start(cmd, args);
    if (input && !input->isEmpty()) {
        write(*input);
    }
    closeWriteChannel();
    loop.exec();

    if (output) {
        *output = outBuffer.trimmed();
    }
    emit done();
    return (exitStatus() == QProcess::NormalExit && exitCode() == 0);
}

bool Cmd::procAsRoot(const QString &cmd, const QStringList &args, QString *output, const QByteArray *input,
                     QuietMode quiet)
{
    return proc(cmd, args, output, input, quiet, Elevation::Yes);
}

bool Cmd::run(const QString &cmd, QuietMode quiet)
{
    return proc("/bin/bash", {"-c", cmd}, nullptr, nullptr, quiet);
}

QString Cmd::readAllOutput()
{
    return outBuffer.trimmed();
}

void Cmd::handleElevationError()
{
#ifdef CLI_BUILD
    MessageHandlerCpp::showMessage(
        MessageHandlerCpp::Critical,
        tr("Administrator Access Required").toStdString(),
        tr("This operation requires administrator privileges. Please restart the "
           "application and enter your password when prompted.")
            .toStdString());
#else
    MessageHandler::showMessage(MessageHandler::Critical, tr("Administrator Access Required"),
                                tr("This operation requires administrator privileges. Please restart the "
                                   "application and enter your password when prompted."));
#endif
    QTimer::singleShot(0, qApp, &QCoreApplication::quit);
    exit(EXIT_FAILURE);
}

#endif
