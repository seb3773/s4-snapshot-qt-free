#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct EmbeddedPayloadView {
    const std::uint8_t *data = nullptr;
    std::uint32_t compressed_size = 0;
    std::uint32_t uncompressed_size = 0;
};

struct EmbeddedTarExtractOptions {
    std::string dest_root;
    std::string strip_prefix;
    std::string entry_prefix;
};

class EmbeddedAssets {
public:
    struct Result {
        bool ok = false;
        std::string error;
    };

    [[nodiscard]] static EmbeddedPayloadView liveFilesPayload();
    [[nodiscard]] static EmbeddedPayloadView isoTemplatesPayload();

    [[nodiscard]] static Result decompressPayload(const EmbeddedPayloadView &payload,
                                                  std::vector<std::uint8_t> &out);

    [[nodiscard]] static Result extractTar(const std::vector<std::uint8_t> &tar_data,
                                           const EmbeddedTarExtractOptions &options);

    [[nodiscard]] static Result extractPayload(const EmbeddedPayloadView &payload,
                                               const EmbeddedTarExtractOptions &options);

    [[nodiscard]] static Result extractLiveFiles(const std::string &dest_root);
    [[nodiscard]] static Result extractIsoTemplates(const std::string &dest_root);

    [[nodiscard]] static Result extractIsoTemplateTree(const std::string &dest_root);
    [[nodiscard]] static Result extractTemplateInitrd(const std::string &dest_root);
};
