# **********************************************************************
# * Copyright (C) 2015-2025 MX Authors
# *
# * Authors: Adrian
# *          Debian <http://debian.org>
# *
# * This file is part of S4 Snapshot.
# *
# * S4 Snapshot is free software: you can redistribute it and/or modify
# * it under the terms of the GNU General Public License as published by
# * the Free Software Foundation, either version 3 of the License, or
# * (at your option) any later version.
# *
# * S4 Snapshot is distributed in the hope that it will be useful,
# * but WITHOUT ANY WARRANTY; without even the implied warranty of
# * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# * GNU General Public License for more details.
# *
# * You should have received a copy of the GNU General Public License
# * along with MX Tools.  If not, see <http://www.gnu.org/licenses/>.
# **********************************************************************

cmake_minimum_required(VERSION 3.16)

# Get version from debian/changelog
execute_process(
    COMMAND dpkg-parsechangelog -SVersion
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE PROJECT_VERSION_FROM_CHANGELOG
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE DPKG_RESULT
)

if(NOT DPKG_RESULT EQUAL 0 OR NOT PROJECT_VERSION_FROM_CHANGELOG)
    message(FATAL_ERROR "Failed to get version from debian/changelog using dpkg-parsechangelog")
endif()

project(s4-snapshot
    VERSION ${PROJECT_VERSION_FROM_CHANGELOG}
    DESCRIPTION "S4 Snapshot - A tool for creating live ISO images from running Debian systems"
    LANGUAGES CXX
)

# Set default build type if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the type of build" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable compile commands export for IDEs and tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Optimize for Ninja builds
if(CMAKE_GENERATOR STREQUAL "Ninja")
    # Enable colored output for Ninja
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_compile_options(-fdiagnostics-color=always)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        add_compile_options(-fcolor-diagnostics)
    endif()
endif()

# Option to use clang for testing builds
option(USE_CLANG "Use clang compiler" OFF)
if(USE_CLANG)
    set(CMAKE_C_COMPILER clang)
    set(CMAKE_CXX_COMPILER clang++)
    set(CMAKE_CXX_COMPILER_ID "Clang")
    message(STATUS "Using clang compiler")
endif()

# Options to control what to build
option(BUILD_GUI "Build GUI version" ON)
option(BUILD_CLI "Build CLI version" ON)
option(BUILD_CLI_QT "Build Qt-based CLI (iso-snapshot-cli-qt)" ON)
option(BUILD_TESTS "Build unit tests" ON)

# Guard rails: qt-free targets must never reintroduce Qt includes or Qt link dependencies.
option(QT_FREE_GUARDS "Fail build if qt-free targets include/link Qt" ON)

# Qt-free i18n for backend/tools/CLI
add_subdirectory(libs/i18n_keyval i18n_keyval_build EXCLUDE_FROM_ALL)

# Qt is optional when building Qt-free targets only.
set(NEED_QT OFF)
if(BUILD_GUI OR BUILD_CLI_QT OR BUILD_TESTS)
    set(NEED_QT ON)
endif()

if(NEED_QT)
    # Find Qt6 components - include all components needed for either build
    set(QT_COMPONENTS Core LinguistTools)
    if(BUILD_GUI)
        list(APPEND QT_COMPONENTS Gui Widgets Concurrent)
    endif()

    find_package(Qt6 REQUIRED COMPONENTS ${QT_COMPONENTS})

    # Enable automatic MOC, UIC, and RCC processing
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTOUIC ON)
    set(CMAKE_AUTORCC ON)
endif()

# Common source files
set(COMMON_SOURCES
    src/log.cpp
    src/logger_cpp.cpp
    src/app_translator_cpp.cpp
    src/i18n_cli.cpp
    src/command_line_parser_std.cpp
    src/qt_library_info_cpp.cpp
    src/qm_translator_cpp.cpp
    src/string_cpp.cpp
    src/settings.cpp
    src/settings_cpp_builder.cpp
    src/settings_exclusions_cpp.cpp
    src/settings_editor_cpp.cpp
    src/settings_filename_cpp.cpp
    src/settings_space_cpp.cpp
    src/settings_validation_cpp.cpp
    src/settings_xdg_user_dirs_cpp.cpp
    src/settings_process_args_cpp.cpp
    src/settings_tempdir_cpp.cpp
    src/settings_snapshotdir_cpp.cpp
    src/settings_debianver_cpp.cpp
    src/settings_liverootspace_cpp.cpp
    src/settings_checkconfiguration_cpp.cpp
    src/batchprocessing.cpp
    src/batchprocessing_cpp.cpp
    src/batchprocessing_cpp_runner.cpp
    src/batchprocessing_cpp_planner.cpp
    legacy_qt_reference/work.cpp
    src/work_cpp_utils.cpp
    src/work_cpp_executor.cpp
    src/work_space_cpp.cpp
    src/tempdir.cpp
    src/tempfile_cpp.cpp
    src/datetime_cpp.cpp
    src/standard_paths_cpp.cpp
    src/process_runner.cpp
    src/command_runner.cpp
    src/filesystemutils_cpp.cpp
    src/systeminfo_cpp.cpp
    src/file_cpp.cpp
    src/stdio_cpp.cpp
    src/dir_cpp.cpp
    src/qsettings_cpp.cpp
    src/work_cpp_planner.cpp
)

set(COMMON_HEADERS
    src/common.h
    src/log.h
    src/logger_cpp.h
    src/app_translator_cpp.h
    src/i18n_cli.h
    src/command_line_parser_std.h
    src/qt_library_info_cpp.h
    src/qm_translator_cpp.h
    src/string_cpp.h
    src/work_cpp_planner.h
    src/work_cpp_executor.h
    src/batchprocessing_cpp_plan.h
    src/batchprocessing_cpp_planner.h
    src/settings_cpp_builder.h
    src/settings_process_args_cpp.h
    src/work_cpp_utils.h
    src/work_space_cpp.h
    src/batchprocessing.h
    src/batchprocessing_cpp.h
    legacy_qt_reference/work.h
    src/tempdir.h
    src/box_type.h
    src/work_cpp_plan.h
    src/settings_tempdir_cpp.h
)

set(HELPER_SOURCES
    src/helper.cpp
    src/process_runner.cpp
    src/process_runner.h
    src/file_cpp.cpp
    src/file_cpp.h
    src/stdio_cpp.cpp
    src/stdio_cpp.h
)

# GUI-specific source files
set(GUI_SOURCES
    src_gui_qt6_cpp_backend/main.cpp
    src_gui_qt6_cpp_backend/mainwindow.cpp
    src/about.cpp
    src/messagehandler.cpp
)

set(GUI_HEADERS
    src_gui_qt6_cpp_backend/mainwindow.h
    src/about.h
    src/messagehandler.h
)

set(UI_FILES
    src_gui_qt6_cpp_backend/mainwindow.ui
)

set(RESOURCE_FILES
    images.qrc
)

# Unit tests (custom, no external framework)
if(BUILD_TESTS)
    add_executable(unit_tests
        tests/unit_tests.cpp
        src/cmd.cpp
        src/cmd.h
        src/cmd_cpp.cpp
        src/cmd_cpp.h
        src/log.cpp
        src/log.h
        src/logger_cpp.cpp
        src/logger_cpp.h
        src/app_translator_cpp.cpp
        src/app_translator_cpp.h
        src/app_translator_qt_oracle.cpp
        src/app_translator_qt_oracle.h
        src/command_line_parser_std.cpp
        src/command_line_parser_std.h
        src/qt_library_info_cpp.cpp
        src/qt_library_info_cpp.h
        src/cli_parser_kv_gen.cpp
        src/cli_parser_kv_gen.h
        src/i18n_cli.cpp
        src/i18n_cli.h
        src/qsettings_cpp.cpp
        src/qsettings_cpp.h
        src/tempfile_cpp.cpp
        src/tempfile_cpp.h
        src/datetime_cpp.cpp
        src/datetime_cpp.h
        src/standard_paths_cpp.cpp
        src/standard_paths_cpp.h
        src/messagehandler_cpp.cpp
        src/messagehandler_cpp.h
        src/messagehandler.cpp
        src/messagehandler.h
        src/qm_translator_cpp.cpp
        src/qm_translator_cpp.h
        src/settings.cpp
        src/settings.h
        src/settings_cpp_builder.cpp
        src/settings_cpp_builder.h
        src/settings_exclusions_cpp.cpp
        src/settings_exclusions_cpp.h
        src/settings_editor_cpp.cpp
        src/settings_editor_cpp.h
        src/settings_filename_cpp.cpp
        src/settings_filename_cpp.h
        src/settings_space_cpp.cpp
        src/settings_space_cpp.h
        src/settings_validation_cpp.cpp
        src/settings_validation_cpp.h
        src/settings_xdg_user_dirs_cpp.cpp
        src/settings_xdg_user_dirs_cpp.h
        src/settings_process_args_cpp.cpp
        src/settings_process_args_cpp.h
        src/settings_tempdir_cpp.cpp
        src/settings_tempdir_cpp.h
        src/settings_snapshotdir_cpp.cpp
        src/settings_snapshotdir_cpp.h
        src/settings_debianver_cpp.cpp
        src/settings_debianver_cpp.h
        src/settings_liverootspace_cpp.cpp
        src/settings_liverootspace_cpp.h
        src/settings_initialization_error_cpp.cpp
        src/settings_initialization_error_cpp.h
        src/settings_checkconfiguration_cpp.cpp
        src/settings_checkconfiguration_cpp.h
        src/batchprocessing_cpp.cpp
        src/batchprocessing_cpp.h
        src/batchprocessing_cpp_runner.cpp
        src/batchprocessing_cpp_runner.h
        src/batchprocessing_cpp_planner.cpp
        src/batchprocessing_cpp_planner.h
        src/batchprocessing_cpp_plan.h
        src/string_cpp.cpp
        src/string_cpp.h
        src/work_cpp_utils.cpp
        src/work_cpp_utils.h
        src/work_cpp_executor.cpp
        src/work_cpp_executor.h
        src/work_space_cpp.cpp
        src/work_space_cpp.h
        src/work_cpp_planner.cpp
        src/work_cpp_planner.h
        src/work_cpp_plan.h
        src/work_bind_root_overlay_cpp.cpp
        src/work_bind_root_overlay_cpp.h
        src/work_bind_root_overlay_cleanup_cpp.cpp
        src/work_bind_root_overlay_cleanup_cpp.h
        legacy_qt_reference/work.cpp
        legacy_qt_reference/work.h
        src/systeminfo_cpp.cpp
        src/systeminfo_cpp.h
        src/systeminfo.cpp
        src/systeminfo.h
        src/filesystemutils.cpp
        src/filesystemutils.h
        src/filesystemutils_cpp.cpp
        src/filesystemutils_cpp.h
        src/command_runner.cpp
        src/command_runner.h
        src/process_runner.cpp
        src/process_runner.h
        src/work_qt_oracle.cpp
        src/work_qt_oracle.h
        src/batchprocessing.cpp
        src/batchprocessing.h
        src/batchprocessing_qt_oracle.cpp
        src/batchprocessing_qt_oracle.h
        src/work_createiso_qt_plan_oracle.cpp
        src/work_createiso_qt_plan_oracle.h
        src/work_copynewiso_qt_plan_oracle.cpp
        src/work_copynewiso_qt_plan_oracle.h
        src/work_setupenv_qt_plan_oracle.cpp
        src/work_setupenv_qt_plan_oracle.h
        src/work_cleanup_qt_plan_oracle.cpp
        src/work_cleanup_qt_plan_oracle.h
        src/batchprocessing_orchestration_qt_plan_oracle.cpp
        src/batchprocessing_orchestration_qt_plan_oracle.h
        src/tempdir.cpp
        src/tempdir.h
        src/file_cpp.cpp
        src/file_cpp.h
        src/stdio_cpp.cpp
        src/stdio_cpp.h
        src/dir_cpp.cpp
        src/dir_cpp.h
    )

    target_link_libraries(unit_tests
        Qt6::Core
        i18n_keyval::i18n_keyval
    )

    target_include_directories(unit_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/legacy_qt_reference
    )

    target_compile_definitions(unit_tests PRIVATE CLI_BUILD=1 UNIT_TESTS=1 PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

    set_target_properties(unit_tests PROPERTIES
        OUTPUT_NAME "unit_tests"
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    )
endif()

# Get all translation files
file(GLOB TRANSLATION_FILES "translations/*.ts")

# Create GUI executable
if(BUILD_GUI)
    add_executable(s4-snapshot
        ${COMMON_SOURCES}
        ${GUI_SOURCES}
        src/cmd_cpp.cpp
        src/cmd_cpp.h
        ${COMMON_HEADERS}
        ${GUI_HEADERS}
        ${UI_FILES}
        ${RESOURCE_FILES}
    )

    # Add include directories for migrated GUI files
    target_include_directories(s4-snapshot PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src_gui_qt6_cpp_backend
        ${CMAKE_SOURCE_DIR}/legacy_qt_reference
    )

    # Link Qt6 libraries for GUI
    target_link_libraries(s4-snapshot
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Concurrent
        i18n_keyval::i18n_keyval
    )

    set_target_properties(s4-snapshot PROPERTIES
        OUTPUT_NAME "s4-snapshot"
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    )
endif()

add_executable(helper
    ${HELPER_SOURCES}
)

target_link_libraries(helper
    i18n_keyval::i18n_keyval
)

set_target_properties(helper PROPERTIES
    OUTPUT_NAME "helper"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

if(QT_FREE_GUARDS)
    add_custom_command(TARGET helper POST_BUILD
        COMMAND /bin/bash -c "set -euo pipefail; grep -nH '#include <Q' ${CMAKE_SOURCE_DIR}/src/helper.cpp ${CMAKE_SOURCE_DIR}/src/process_runner.cpp ${CMAKE_SOURCE_DIR}/src/file_cpp.cpp ${CMAKE_SOURCE_DIR}/src/stdio_cpp.cpp && { echo 'ERROR: Qt include found in qt-free target helper'; exit 1; } || true"
        COMMAND /bin/bash -c "set -euo pipefail; ldd $<TARGET_FILE:helper> | grep -i qt && { echo 'ERROR: Qt library linked into qt-free target helper'; exit 1; } || true"
        VERBATIM
    )
endif()

# Dev-time Qt-free tool: generate vendored cli_parser locale .kv files from an input .qm
add_executable(gen_cli_parser_kv
    src/gen_cli_parser_kv.cpp
    src/cli_parser_kv_gen.cpp
    src/cli_parser_kv_gen.h
    src/qm_translator_cpp.cpp
    src/qm_translator_cpp.h
    src/i18n_cli.cpp
    src/i18n_cli.h
    src/file_cpp.cpp
    src/file_cpp.h
)

target_link_libraries(gen_cli_parser_kv
    i18n_keyval::i18n_keyval
)

set_target_properties(gen_cli_parser_kv PROPERTIES
    OUTPUT_NAME "gen_cli_parser_kv"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

if(QT_FREE_GUARDS)
    add_custom_command(TARGET gen_cli_parser_kv POST_BUILD
        COMMAND /bin/bash -c "set -euo pipefail; grep -nH '#include <Q' ${CMAKE_SOURCE_DIR}/src/gen_cli_parser_kv.cpp ${CMAKE_SOURCE_DIR}/src/cli_parser_kv_gen.cpp ${CMAKE_SOURCE_DIR}/src/qm_translator_cpp.cpp ${CMAKE_SOURCE_DIR}/src/i18n_cli.cpp ${CMAKE_SOURCE_DIR}/src/file_cpp.cpp && { echo 'ERROR: Qt include found in qt-free target gen_cli_parser_kv'; exit 1; } || true"
        COMMAND /bin/bash -c "set -euo pipefail; ldd $<TARGET_FILE:gen_cli_parser_kv> | grep -i qt && { echo 'ERROR: Qt library linked into qt-free target gen_cli_parser_kv'; exit 1; } || true"
        VERBATIM
    )
endif()

# Create CLI executable
if(BUILD_CLI)
    if(BUILD_CLI_QT)
        add_executable(iso-snapshot-cli-qt
            ${COMMON_SOURCES}
            src/main.cpp
            src/cmd_cpp.cpp
            src/messagehandler_cpp.cpp
            ${COMMON_HEADERS}
            src/cmd_cpp.h
            src/messagehandler_cpp.h
            ${RESOURCE_FILES}
        )

        # Set CLI build definition
        target_compile_definitions(iso-snapshot-cli-qt PRIVATE CLI_BUILD=1)

        # Link Qt6 libraries for CLI
        target_link_libraries(iso-snapshot-cli-qt
            Qt6::Core
            i18n_keyval::i18n_keyval
        )

        target_include_directories(iso-snapshot-cli-qt PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/legacy_qt_reference
        )

        set_target_properties(iso-snapshot-cli-qt PROPERTIES
            OUTPUT_NAME "iso-snapshot-cli-qt"
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
        )
    endif()

    # Qt-free CLI (final target)
    add_executable(iso-snapshot-cli
        src/main_cli_cpp.cpp
        src/log.cpp
        src/logger_cpp.cpp
        src/app_translator_cpp.cpp
        src/i18n_cli.cpp
        src/command_line_parser_std.cpp
        src/qt_library_info_cpp.cpp
        src/qm_translator_cpp.cpp
        src/cli_parser_kv_gen.cpp
        src/string_cpp.cpp
        src/settings_cpp_builder.cpp
        src/settings_exclusions_cpp.cpp
        src/settings_editor_cpp.cpp
        src/settings_filename_cpp.cpp
        src/settings_space_cpp.cpp
        src/settings_xdg_user_dirs_cpp.cpp
        src/settings_process_args_cpp.cpp
        src/settings_validation_cpp.cpp
        src/settings_tempdir_cpp.cpp
        src/settings_snapshotdir_cpp.cpp
        src/settings_debianver_cpp.cpp
        src/settings_liverootspace_cpp.cpp
        src/settings_initialization_error_cpp.cpp
        src/settings_checkconfiguration_cpp.cpp
        src/batchprocessing_cpp.cpp
        src/batchprocessing_cpp_runner.cpp
        src/batchprocessing_cpp_planner.cpp
        src/work_cpp_utils.cpp
        src/work_cpp_executor.cpp
        src/work_cpp_planner.cpp
        src/work_bind_root_overlay_cpp.cpp
        src/work_space_cpp.cpp
        src/messagehandler_cpp.cpp
        src/tempdir.cpp
        src/tempfile_cpp.cpp
        src/datetime_cpp.cpp
        src/standard_paths_cpp.cpp
        src/process_runner.cpp
        src/command_runner.cpp
        src/filesystemutils_cpp.cpp
        src/systeminfo_cpp.cpp
        src/file_cpp.cpp
        src/stdio_cpp.cpp
        src/dir_cpp.cpp
        src/qsettings_cpp.cpp
    )

    target_compile_definitions(iso-snapshot-cli PRIVATE CLI_BUILD=1)

    target_link_libraries(iso-snapshot-cli
        i18n_keyval::i18n_keyval
    )

    set_target_properties(iso-snapshot-cli PROPERTIES
        OUTPUT_NAME "iso-snapshot-cli"
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    )

    if(QT_FREE_GUARDS)
        add_custom_command(TARGET iso-snapshot-cli POST_BUILD
            COMMAND /bin/bash -c "set -euo pipefail; grep -nH '#include <Q' ${CMAKE_SOURCE_DIR}/src/main_cli_cpp.cpp ${CMAKE_SOURCE_DIR}/src/log.cpp ${CMAKE_SOURCE_DIR}/src/logger_cpp.cpp ${CMAKE_SOURCE_DIR}/src/app_translator_cpp.cpp ${CMAKE_SOURCE_DIR}/src/i18n_cli.cpp ${CMAKE_SOURCE_DIR}/src/command_line_parser_std.cpp ${CMAKE_SOURCE_DIR}/src/qt_library_info_cpp.cpp ${CMAKE_SOURCE_DIR}/src/qm_translator_cpp.cpp ${CMAKE_SOURCE_DIR}/src/cli_parser_kv_gen.cpp ${CMAKE_SOURCE_DIR}/src/string_cpp.cpp ${CMAKE_SOURCE_DIR}/src/settings_cpp_builder.cpp ${CMAKE_SOURCE_DIR}/src/settings_exclusions_cpp.cpp ${CMAKE_SOURCE_DIR}/src/settings_editor_cpp.cpp ${CMAKE_SOURCE_DIR}/src/settings_filename_cpp.cpp ${CMAKE_SOURCE_DIR}/src/settings_space_cpp.cpp ${CMAKE_SOURCE_DIR}/src/settings_xdg_user_dirs_cpp.cpp ${CMAKE_SOURCE_DIR}/src/settings_process_args_cpp.cpp ${CMAKE_SOURCE_DIR}/src/batchprocessing_cpp.cpp ${CMAKE_SOURCE_DIR}/src/batchprocessing_cpp_runner.cpp ${CMAKE_SOURCE_DIR}/src/batchprocessing_cpp_planner.cpp ${CMAKE_SOURCE_DIR}/src/work_cpp_utils.cpp ${CMAKE_SOURCE_DIR}/src/work_cpp_executor.cpp ${CMAKE_SOURCE_DIR}/src/work_cpp_planner.cpp ${CMAKE_SOURCE_DIR}/src/work_space_cpp.cpp ${CMAKE_SOURCE_DIR}/src/messagehandler_cpp.cpp ${CMAKE_SOURCE_DIR}/src/tempdir.cpp ${CMAKE_SOURCE_DIR}/src/tempfile_cpp.cpp ${CMAKE_SOURCE_DIR}/src/datetime_cpp.cpp ${CMAKE_SOURCE_DIR}/src/standard_paths_cpp.cpp ${CMAKE_SOURCE_DIR}/src/process_runner.cpp ${CMAKE_SOURCE_DIR}/src/command_runner.cpp ${CMAKE_SOURCE_DIR}/src/filesystemutils_cpp.cpp ${CMAKE_SOURCE_DIR}/src/systeminfo_cpp.cpp ${CMAKE_SOURCE_DIR}/src/file_cpp.cpp ${CMAKE_SOURCE_DIR}/src/stdio_cpp.cpp ${CMAKE_SOURCE_DIR}/src/dir_cpp.cpp ${CMAKE_SOURCE_DIR}/src/qsettings_cpp.cpp && { echo 'ERROR: Qt include found in qt-free target iso-snapshot-cli'; exit 1; } || true"
            COMMAND /bin/bash -c "set -euo pipefail; ldd $<TARGET_FILE:iso-snapshot-cli> | grep -i qt && { echo 'ERROR: Qt library linked into qt-free target iso-snapshot-cli'; exit 1; } || true"
            VERBATIM
        )
    endif()
endif()

# Function to set common compile options and definitions
function(set_common_properties target)
    # Set compiler flags
    target_compile_options(${target} PRIVATE
        -Wpedantic
        -pedantic
        -Werror=return-type
        -Werror=switch
        -Werror=uninitialized
        -Werror
    )

    # Add compiler-specific flags
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR USE_CLANG)
        target_compile_options(${target} PRIVATE -Werror=return-stack-address)
    else()
        target_compile_options(${target} PRIVATE -Werror=return-local-addr)
    endif()

    # Set compile definitions
    target_compile_definitions(${target} PRIVATE
        QT_DEPRECATED_WARNINGS
        QT_DISABLE_DEPRECATED_BEFORE=0x060000
        VERSION="${PROJECT_VERSION}"
    )

    # Build type specific optimizations
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_definitions(${target} PRIVATE NDEBUG)
        target_compile_options(${target} PRIVATE -O3)

        # Add LTO - different flags for different compilers
        if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR USE_CLANG)
            target_compile_options(${target} PRIVATE -flto=thin)
            target_link_options(${target} PRIVATE -flto=thin)
        else()
            target_compile_options(${target} PRIVATE -flto=auto)
            target_link_options(${target} PRIVATE -flto=auto)
        endif()
    elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(${target} PRIVATE -g -O0)
        target_compile_definitions(${target} PRIVATE DEBUG)
    endif()
endfunction()

# Apply common properties to targets
if(BUILD_GUI)
    set_common_properties(s4-snapshot)
endif()

if(BUILD_CLI)
    if(BUILD_CLI_QT)
        set_common_properties(iso-snapshot-cli-qt)
    endif()
    set_common_properties(iso-snapshot-cli)
endif()

set_common_properties(helper)
set_common_properties(gen_cli_parser_kv)

# Handle translations
if(NEED_QT)
    if(BUILD_GUI)
        qt6_add_translations(s4-snapshot
            TS_FILES ${TRANSLATION_FILES}
            LRELEASE_OPTIONS -compress -nounfinished -removeidentical -silent
            QM_FILES_OUTPUT_VARIABLE qm_files
        )
    endif()

    if(BUILD_CLI AND BUILD_CLI_QT)
        qt6_add_translations(iso-snapshot-cli-qt
            TS_FILES ${TRANSLATION_FILES}
            LRELEASE_OPTIONS -compress -nounfinished -removeidentical -silent
            QM_FILES_OUTPUT_VARIABLE qm_files
        )
    endif()
endif()

# Installation targets (required by Debian build system)
# Other files are handled by debian/install
if(BUILD_GUI)
    install(TARGETS s4-snapshot
        RUNTIME DESTINATION bin
    )
endif()

if(BUILD_CLI)
    install(TARGETS iso-snapshot-cli
        RUNTIME DESTINATION bin
    )
endif()
