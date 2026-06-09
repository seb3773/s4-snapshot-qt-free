set(EMBED_BUILD_DIR "${CMAKE_BINARY_DIR}/embed")
set(EMBED_GENERATED_DIR "${EMBED_BUILD_DIR}/generated")
set(EMBED_LIVE_FILES_HEADER "${EMBED_GENERATED_DIR}/live_files_payload.h")
set(EMBED_ISO_TEMPLATES_HEADER "${EMBED_GENERATED_DIR}/iso_templates_payload.h")
set(EMBED_RUNTIME_SCRIPTS_HEADER "${EMBED_GENERATED_DIR}/runtime_scripts_payload.h")

set(EMBED_BUILD_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/build_embedded_payloads.sh")

set(EMBED_INPUT_LIVE_FILES "${CMAKE_SOURCE_DIR}/data/live-files")
set(EMBED_INPUT_ISO_TEMPLATES "${CMAKE_SOURCE_DIR}/data/iso-templates")
set(EMBED_INPUT_RUNTIME_SCRIPTS
    "${CMAKE_SOURCE_DIR}/scripts/snapshot-bootparameter.sh"
    "${CMAKE_SOURCE_DIR}/scripts/configure_debian_calamares.sh"
    "${CMAKE_SOURCE_DIR}/scripts/copy-initrd-modules"
    "${CMAKE_SOURCE_DIR}/scripts/copy-initrd-programs"
)

add_custom_command(
    OUTPUT "${EMBED_LIVE_FILES_HEADER}" "${EMBED_ISO_TEMPLATES_HEADER}" "${EMBED_RUNTIME_SCRIPTS_HEADER}"
    COMMAND ${CMAKE_COMMAND} -E env EMBED_BUILD_DIR=${EMBED_BUILD_DIR} bash ${EMBED_BUILD_SCRIPT}
    DEPENDS
        ${EMBED_BUILD_SCRIPT}
        ${CMAKE_SOURCE_DIR}/tools/asset_pack.c
        ${CMAKE_SOURCE_DIR}/tools/asset_unpack.c
        ${EMBED_INPUT_ISO_TEMPLATES}
        ${EMBED_INPUT_LIVE_FILES}
        ${EMBED_INPUT_RUNTIME_SCRIPTS}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Generating embedded LZ4 asset payloads"
    VERBATIM
)

add_library(embedded_assets STATIC
    ${CMAKE_SOURCE_DIR}/src/embedded/embedded_assets.cpp
    ${CMAKE_SOURCE_DIR}/src/embedded/embedded_assets_runtime.cpp
    ${CMAKE_SOURCE_DIR}/src/embedded/embedded_payload_live_files.cpp
    ${CMAKE_SOURCE_DIR}/src/embedded/embedded_payload_iso_templates.cpp
    ${CMAKE_SOURCE_DIR}/src/embedded/embedded_payload_runtime_scripts.cpp
    ${CMAKE_SOURCE_DIR}/third_party/microtar/microtar.c
    ${CMAKE_SOURCE_DIR}/third_party/microtar/mtar_memory.c
    ${CMAKE_SOURCE_DIR}/decompressor/lz4/codec_lz4.c
)

add_custom_target(embedded_assets_generate DEPENDS
    "${EMBED_LIVE_FILES_HEADER}"
    "${EMBED_ISO_TEMPLATES_HEADER}"
    "${EMBED_RUNTIME_SCRIPTS_HEADER}"
)

add_dependencies(embedded_assets embedded_assets_generate)

target_include_directories(embedded_assets
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src/embedded
        ${EMBED_GENERATED_DIR}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/third_party/microtar
        ${CMAKE_SOURCE_DIR}/decompressor/lz4
)

target_compile_features(embedded_assets PUBLIC cxx_std_20)

set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/embedded/embedded_payload_iso_templates.cpp
    ${CMAKE_SOURCE_DIR}/src/embedded/embedded_payload_runtime_scripts.cpp
    PROPERTIES
        COMPILE_FLAGS "-O1"
)
