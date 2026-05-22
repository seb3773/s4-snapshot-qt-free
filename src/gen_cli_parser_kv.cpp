#include "cli_parser_kv_gen.h"

#include <cstdio>
#include <string>

namespace {

static void print_usage(const char *argv0)
{
    std::fprintf(stderr,
                 "Usage:\n"
                 "  %s <input_qm_path> <out_kv_path>\n"
                 "\n"
                 "Notes:\n"
                 "  - Tool is Qt-free; it reads .qm using QmTranslatorCpp.\n"
                 "  - It writes key=value UTF-8 kv file for cli parser internal strings.\n",
                 argv0 ? argv0 : "gen_cli_parser_kv");
}

} // namespace

int main(int argc, char *argv[])
{
    if (argc != 3) {
        print_usage(argv[0]);
        return 2;
    }

    const std::string qmPath = argv[1] ? std::string(argv[1]) : std::string();
    const std::string outPath = argv[2] ? std::string(argv[2]) : std::string();
    if (qmPath.empty() || outPath.empty()) {
        print_usage(argv[0]);
        return 2;
    }

    int missing = 0;
    const auto entries = CliParserKvGen::generateFromQmOrFallback(qmPath, &missing);

    if (!CliParserKvGen::writeKvFile(outPath, entries)) {
        std::fprintf(stderr, "Error: could not write output file: %s\n", outPath.c_str());
        return 3;
    }

    if (missing != 0) {
        std::fprintf(stderr,
                     "Warning: %d translations missing in input .qm; fell back to sourceText for those entries.\n",
                     missing);
        return 1;
    }

    return 0;
}
