#pragma once

#include <string>

#include <cstdio>

class LoggerCpp
{
public:
    enum class Level { Info, Debug, Warning, Critical, Fatal };

    static void writeRaw(FILE *f, const std::string &bytes);
    static void writeStdoutRaw(const std::string &bytes);
    static void writeLine(FILE *f, const std::string &utf8Line);
    static void writeLineStdoutUtf8(const std::string &utf8Line);

    [[nodiscard]] static const char *levelTag(Level level);
    [[nodiscard]] static std::string formatFileLine(Level level, const std::string &utf8Message);

    static void logTo(FILE *f, Level level, const std::string &utf8Message);
    static void log(Level level, const std::string &utf8Message);
};
