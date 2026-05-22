#pragma once

#if defined(CLI_BUILD) && !defined(UNIT_TESTS)

// Production CLI/backend: Qt-free API only
#include "cmd_cpp.h"

using Cmd = CmdCpp;

#else

#include <QByteArray>
#include <QString>
#include <QStringList>

class QObject;

#ifndef CLI_BUILD
#include <QProcess>
#endif

#ifndef CLI_BUILD
class Cmd : public QProcess
{
    Q_OBJECT
public:
    explicit Cmd(QObject *parent = nullptr);

    enum class QuietMode { No, Yes };
    enum class Elevation { No, Yes };

    // Returns the elevation tool path appropriate for current mode (CLI/GUI),
    // preferring sudo in CLI, pkexec in GUI. Empty if none found.
    static QString elevationTool();
    static bool isCliMode();
    static QString loggedInUserName();

    bool proc(const QString &cmd, const QStringList &args = {}, QString *output = nullptr,
              const QByteArray *input = nullptr, QuietMode quiet = QuietMode::No,
              Elevation elevation = Elevation::No);
    bool procAsRoot(const QString &cmd, const QStringList &args = {}, QString *output = nullptr,
                    const QByteArray *input = nullptr, QuietMode quiet = QuietMode::No);
    [[nodiscard]] QString getOut(const QString &cmd, QuietMode quiet = QuietMode::No);
    [[nodiscard]] QString getOutAsRoot(const QString &cmd, const QStringList &args = {},
                                       QuietMode quiet = QuietMode::No);
    [[nodiscard]] QString readAllOutput();
    bool run(const QString &cmd, QuietMode quiet = QuietMode::No);

signals:
    void done();
    void errorAvailable(const QString &err);
    void outputAvailable(const QString &out);

private:
    const QString elevationToolPath;
    const QString helperPath;
    QString outBuffer;
    static constexpr int EXIT_CODE_COMMAND_NOT_FOUND = 127;
    static constexpr int EXIT_CODE_PERMISSION_DENIED = 126;

    bool helperProc(const QStringList &helperArgs, QString *output = nullptr, const QByteArray *input = nullptr,
                    QuietMode quiet = QuietMode::No);
    [[nodiscard]] static QStringList helperExecArgs(const QString &cmd, const QStringList &args);
    void handleElevationError();
};

#else

class Cmd
{
public:
    explicit Cmd(QObject *parent = nullptr);

    enum class QuietMode { No, Yes };
    enum class Elevation { No, Yes };

    static QString elevationTool();
    static bool isCliMode();
    static QString loggedInUserName();

    bool proc(const QString &cmd, const QStringList &args = {}, QString *output = nullptr,
              const QByteArray *input = nullptr, QuietMode quiet = QuietMode::No,
              Elevation elevation = Elevation::No);
    bool procAsRoot(const QString &cmd, const QStringList &args = {}, QString *output = nullptr,
                    const QByteArray *input = nullptr, QuietMode quiet = QuietMode::No);
    [[nodiscard]] QString getOut(const QString &cmd, QuietMode quiet = QuietMode::No);
    [[nodiscard]] QString getOutAsRoot(const QString &cmd, const QStringList &args = {},
                                       QuietMode quiet = QuietMode::No);
    [[nodiscard]] QString readAllOutput();
    bool run(const QString &cmd, QuietMode quiet = QuietMode::No);

private:
    const QString elevationToolPath;
    const QString helperPath;
    QString outBuffer;
    int lastExitCode = 0;
    bool lastNormalExit = true;
    static constexpr int EXIT_CODE_COMMAND_NOT_FOUND = 127;
    static constexpr int EXIT_CODE_PERMISSION_DENIED = 126;

    bool helperProc(const QStringList &helperArgs, QString *output = nullptr, const QByteArray *input = nullptr,
                    QuietMode quiet = QuietMode::No);
    [[nodiscard]] static QStringList helperExecArgs(const QString &cmd, const QStringList &args);
    void handleElevationError();
};

#endif

#endif
