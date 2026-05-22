#pragma once

#ifdef CLI_BUILD

#include "log.h"
#include "logger_cpp.h"

#include <QString>
#include <QStringList>
#include <QByteArray>

#include <sstream>
#include <string>
#include <type_traits>

template <LoggerCpp::Level Level>
class QtLogStreamCli
{
public:
    QtLogStreamCli() = default;

    QtLogStreamCli &noquote() { return *this; }

    QtLogStreamCli &operator<<(const QString &s)
    {
        appendSpaceIfNeeded();
        const QByteArray b = s.toUtf8();
        buf.append(b.constData(), static_cast<size_t>(b.size()));
        return *this;
    }

    QtLogStreamCli &operator<<(const QStringList &list)
    {
        appendSpaceIfNeeded();
        buf.push_back('(');
        for (int i = 0; i < list.size(); ++i) {
            if (i != 0) {
                buf.append(", ");
            }
            buf.push_back('"');
            const QByteArray b = list.at(i).toUtf8();
            buf.append(b.constData(), static_cast<size_t>(b.size()));
            buf.push_back('"');
        }
        buf.push_back(')');
        return *this;
    }

    QtLogStreamCli &operator<<(const QByteArray &b)
    {
        appendSpaceIfNeeded();
        buf.append(b.constData(), static_cast<size_t>(b.size()));
        return *this;
    }

    QtLogStreamCli &operator<<(const char *s)
    {
        appendSpaceIfNeeded();
        buf.append(s ? s : "(null)");
        return *this;
    }

    QtLogStreamCli &operator<<(char c)
    {
        appendSpaceIfNeeded();
        buf.push_back(c);
        return *this;
    }

    QtLogStreamCli &operator<<(const std::string &s)
    {
        appendSpaceIfNeeded();
        buf.append(s);
        return *this;
    }

    template <typename T, typename std::enable_if<std::is_arithmetic<T>::value, int>::type = 0>
    QtLogStreamCli &operator<<(const T &v)
    {
        appendSpaceIfNeeded();
        std::ostringstream oss;
        oss << v;
        buf.append(oss.str());
        return *this;
    }

    ~QtLogStreamCli() { Log::writeMessage(Level, buf); }

private:
    void appendSpaceIfNeeded()
    {
        if (!first) {
            buf.push_back(' ');
        }
        first = false;
    }

    bool first {true};
    std::string buf;
};

#ifdef qDebug
#undef qDebug
#endif
#ifdef qWarning
#undef qWarning
#endif
#ifdef qCritical
#undef qCritical
#endif

#define qDebug() QtLogStreamCli<LoggerCpp::Level::Debug>()
#define qWarning() QtLogStreamCli<LoggerCpp::Level::Warning>()
#define qCritical() QtLogStreamCli<LoggerCpp::Level::Critical>()

#endif
