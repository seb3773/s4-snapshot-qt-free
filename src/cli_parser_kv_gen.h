#pragma once

#include <string>
#include <string_view>
#include <vector>

struct CliParserKvEntry {
    std::string key;
    std::string value;
};

namespace CliParserKvGen
{
[[nodiscard]] std::vector<CliParserKvEntry> generateFromQmOrFallback(const std::string &qmFilePath,
                                                                    int *outMissingCount);

[[nodiscard]] bool writeKvFile(const std::string &outKvFilePath,
                              const std::vector<CliParserKvEntry> &entries);
}
