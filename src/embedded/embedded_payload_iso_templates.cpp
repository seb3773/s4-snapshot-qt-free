#include "embedded_assets.h"

#include "iso_templates_payload.h"

EmbeddedPayloadView EmbeddedAssets::isoTemplatesPayload()
{
    return EmbeddedPayloadView{
        embedded_iso_templates_payload,
        embedded_iso_templates_compressed_size,
        embedded_iso_templates_uncompressed_size,
    };
}
