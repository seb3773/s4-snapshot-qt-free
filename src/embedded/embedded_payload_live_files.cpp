#include "embedded_assets.h"

#include "live_files_payload.h"

EmbeddedPayloadView EmbeddedAssets::liveFilesPayload()
{
    return EmbeddedPayloadView{
        embedded_live_files_payload,
        embedded_live_files_compressed_size,
        embedded_live_files_uncompressed_size,
    };
}
