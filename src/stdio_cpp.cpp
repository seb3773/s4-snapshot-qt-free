#include "stdio_cpp.h"

#include <cerrno>
#include <cstring>
#include <cctype>

std::string StdioCpp::readLine(FILE *stream)
{
    if (!stream) {
        return std::string();
    }

    std::string out;
    int ch = 0;
    while ((ch = std::fgetc(stream)) != EOF) {
        if (ch == '\n') {
            break;
        }
        out.push_back(static_cast<char>(ch));
    }

    // Match QTextStream::readLine(): strips "\n" and "\r\n".
    if (!out.empty() && out.back() == '\r') {
        out.pop_back();
    }

    if (ch == EOF && out.empty()) {
        return std::string();
    }

    return out;
}

std::string StdioCpp::readWord(FILE *stream)
{
    if (!stream) {
        return std::string();
    }

    std::string out;

    int ch = 0;

    // Skip leading whitespace.
    while ((ch = std::fgetc(stream)) != EOF) {
        if (!std::isspace(static_cast<unsigned char>(ch))) {
            out.push_back(static_cast<char>(ch));
            break;
        }
    }

    if (ch == EOF) {
        return std::string();
    }

    // Consume until next whitespace.
    while ((ch = std::fgetc(stream)) != EOF) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            break;
        }
        out.push_back(static_cast<char>(ch));
    }

    return out;
}

bool StdioCpp::write(FILE *stream, const std::string &s)
{
    if (!stream) {
        return false;
    }
    if (s.empty()) {
        return true;
    }
    const std::size_t n = std::fwrite(s.data(), 1, s.size(), stream);
    return n == s.size();
}

bool StdioCpp::flush(FILE *stream)
{
    if (!stream) {
        return false;
    }
    return std::fflush(stream) == 0;
}
