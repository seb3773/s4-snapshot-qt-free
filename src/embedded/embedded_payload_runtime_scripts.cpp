#include "embedded_assets.h"

#include "runtime_scripts_payload.h"

EmbeddedPayloadView EmbeddedAssets::runtimeScriptsPayload()
{
    return EmbeddedPayloadView{
        embedded_runtime_scripts_payload,
        embedded_runtime_scripts_compressed_size,
        embedded_runtime_scripts_uncompressed_size,
    };
}
