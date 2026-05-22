#include "logger_cpp.h"

#include "datetime_cpp.h"
#include "stdio_cpp.h"

#include <cstdio>

namespace {

bool containsChar(const std::string &s, char ch)
{
    return s.find(ch) != std::string::npos;
}

bool startsWith(const std::string &s, const char *prefix)
{
    const std::string p(prefix);
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

} // namespace

const char *LoggerCpp::levelTag(Level level)
{
    switch (level) {
    case LoggerCpp::Level::Info:
        return "INF";
    case LoggerCpp::Level::Debug:
        return "DBG";
    case LoggerCpp::Level::Warning:
        return "WRN";
    case LoggerCpp::Level::Critical:
        return "CRT";
    case LoggerCpp::Level::Fatal:
        return "FTL";
    }
    return "DBG";
}

std::string LoggerCpp::formatFileLine(Level level, const std::string &utf8Message)
{
    return DateTimeCpp::nowLocalYmdHmsMillis() + levelTag(level) + std::string(": ") + utf8Message + "\n";
}

void LoggerCpp::writeStdoutRaw(const std::string &bytes)
{
    writeRaw(stdout, bytes);
}

void LoggerCpp::writeLineStdoutUtf8(const std::string &utf8Line)
{
    writeLine(stdout, utf8Line);
}

void LoggerCpp::writeRaw(FILE *f, const std::string &bytes)
{
    (void)StdioCpp::write(f, bytes);
}

void LoggerCpp::writeLine(FILE *f, const std::string &utf8Line)
{
    writeRaw(f, utf8Line + "\n");
}

void LoggerCpp::logTo(FILE *f, Level level, const std::string &utf8Message)
{
    (void)level;
    if (containsChar(utf8Message, '\r') || startsWith(utf8Message, "\033[2K")) {
        writeRaw(f, std::string("\033[?25l") + utf8Message);
        return;
    }
    writeLine(f, utf8Message);
}

void LoggerCpp::log(Level level, const std::string &utf8Message)
{
    logTo(stdout, level, utf8Message);
}
