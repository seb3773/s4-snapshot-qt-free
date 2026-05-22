#ifndef STDIO_CPP_H
#define STDIO_CPP_H

#include <cstdio>
#include <string>

class StdioCpp
{
public:
    // Reads a single line from stream.
    // Behavior is tailored to match QTextStream::readLine():
    // - returns the line without trailing "\n" or "\r\n"
    // - returns empty string on EOF
    [[nodiscard]] static std::string readLine(FILE *stream);

    // Reads a whitespace-delimited word from stream.
    // Leading whitespace is skipped. Returns empty string on EOF.
    [[nodiscard]] static std::string readWord(FILE *stream);

    [[nodiscard]] static bool write(FILE *stream, const std::string &s);

    [[nodiscard]] static bool flush(FILE *stream);
};

#endif
