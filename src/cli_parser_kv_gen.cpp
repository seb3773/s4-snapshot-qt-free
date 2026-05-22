#include "cli_parser_kv_gen.h"

#include "i18n_cli.h"
#include "qm_translator_cpp.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace {

struct NeededQtString {
    const char *context;
    const char *sourceText;
    const char *comment;
};

static const NeededQtString kNeeded[] = {
    {"QCommandLineParser", "Displays version information.", ""},
    {"QCommandLineParser", "Displays help on commandline options.", ""},
    {"QCommandLineParser", "Displays help, including generic Qt options.", ""},
    {"QCommandLineParser", "Unknown option '%1'.", ""},
    {"QCommandLineParser", "Unknown options: %1.", ""},
    {"QCommandLineParser", "Missing value after '%1'.", ""},
    {"QCommandLineParser", "Unexpected value after '%1'.", ""},
    {"QCommandLineParser", "[options]", ""},
    {"QCommandLineParser", "Usage: %1", ""},
    {"QCommandLineParser", "Options:", ""},
    {"QCommandLineParser", "Arguments:", ""},
};

} // namespace

namespace CliParserKvGen
{

std::vector<CliParserKvEntry> generateFromQmOrFallback(const std::string &qmFilePath,
                                                      int *outMissingCount)
{
    int missing = 0;

    QmTranslatorCpp tr;
    const bool loaded = tr.loadFile(qmFilePath);

    std::vector<CliParserKvEntry> out;
    out.reserve(std::size(kNeeded));

    for (const auto &s : kNeeded) {
        const std::string key = I18nCli::makeQtKey(s.context, s.sourceText, s.comment);

        std::string value;
        if (loaded) {
            const auto opt = tr.translate(s.context, s.sourceText, s.comment);
            if (opt.has_value()) {
                value = *opt;
            } else {
                ++missing;
                value = s.sourceText;
            }
        } else {
            ++missing;
            value = s.sourceText;
        }

        out.push_back({key, value});
    }

    std::sort(out.begin(), out.end(), [](const CliParserKvEntry &a, const CliParserKvEntry &b) {
        return a.key < b.key;
    });

    if (outMissingCount) {
        *outMissingCount = missing;
    }

    return out;
}

bool writeKvFile(const std::string &outKvFilePath,
                 const std::vector<CliParserKvEntry> &entries)
{
    const std::filesystem::path outPath(outKvFilePath);
    if (outPath.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(outPath.parent_path(), ec);
        if (ec) {
            return false;
        }
    }

    std::ofstream f(outKvFilePath, std::ios::binary | std::ios::trunc);
    if (!f) {
        return false;
    }

    for (const auto &e : entries) {
        f << e.key << "=" << e.value << "\n";
        if (!f) {
            return false;
        }
    }

    return true;
}

} // namespace CliParserKvGen
