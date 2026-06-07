# S4 Snapshot - Pure C++ Port

A complete port of the S4 Snapshot backend from Qt6 to pure C++ with zero Qt dependencies for CLI and backend tools and (hopefully) 100% functional equivalence.

Components:

- Backend: pure C++ (Qt-free)
- CLI: pure C++ (Qt-free) 
- GUI: Qt6 (uses pure C++ backend)
- Tools: pure C++ (Qt-free)

Summary: S4 Snapshot creates bootable live ISO images from running Debian-based systems. This port removes all Qt dependencies from the backend and CLI while preserving the Qt6 GUI as an optional interface.

### what we have:

- **zero Qt dependencies**: CLI binary (`iso-snapshot-cli`) and helper tools have no Qt runtime requirements
- **100% functional Equivalence**: Byte-for-byte output validation against original Qt implementation
- **clean architecture**: "Plan-Execute" pattern with immutable data structures
- **embedded runtime assets**: live-files and ISO templates are compiled into the CLI/GUI binaries
- **granular privilege elevation**: the main frontend runs as the user; only allowlisted helper actions run as root
- **comprehensive tests**: 60+ Qt primitives replaced and validated through oracle tests

## Architecture:

### Component Structure

```
┌─────────────────────────────────────────────────────────────┐
│                     User Interfaces                         │
├───────────────────────────┬─────────────────────────────────┤
│ Qt6 GUI                    │ CLI / other frontends           │
│ (src_gui_qt6_cpp_backend/) │ (main_cli_cpp, TUI, custom UI)  │
└──────────────┬────────────┴──────────────┬──────────────────┘
               │                           │
               └───────────────────────────┘
                            │
         ┌──────────────────▼──────────────────┐
         │   BatchprocessingCppRunner          │
         │   (main orchestration entry point)  │
         └──────────────────┬──────────────────┘
                            │
         ┌──────────────────▼──────────────────┐
         │   BatchprocessingCppPlanner         │
         │   (high-level planning)             │
         └──────────────────┬──────────────────┘
                            │
         ┌──────────────────▼──────────────────┐
         │   WorkCppPlanner                    │
         │   (Work method planning)            │
         └──────────────────┬──────────────────┘
                            │
         ┌──────────────────▼──────────────────┐
         │   WorkCppExecutor                   │
         │   (command execution engine)        │
         └───────────────┬───────────┬─────────┘
                         │           │
        ┌────────────────▼───┐   ┌───▼────────────────────┐
        │ Embedded assets     │   │ Privileged helper      │
        │ workDir/_embedded/  │   │ /usr/lib/<app>/helper  │
        │ + workDir/iso-tree  │   │ exec <allowed-action>  │
        └────────────────────┘   └────────────────────────┘
```

The helper is not a frontend. It is the privileged boundary used by frontends
when the backend needs root-only operations such as mounts, ownership changes,
`mksquashfs`, cleanup, or the native `installed-to-live` workflow.

### Plan-Execute Pattern

The backend uses an immutable plan-execute architecture:

 **Planning phase**: Creates immutable `WorkCppPlan` describing all operations
 **Execution phase**: `WorkCppExecutor` interprets and executes the plan
 **benefits**: Inspectable plans, reproducible results, clear separation of concerns

## Structure:

```
s4-snapshot-port/
├── src/                          # Core implementation
│   ├── *_cpp.{h,cpp}            # Pure C++ backend (Qt-free)
│   ├── work_cpp_*.{h,cpp}       # Work method implementations
│   ├── batchprocessing_cpp_*.{h,cpp}  # Orchestration layer
│   ├── settings_cpp*.{h,cpp}    # Configuration management
│   ├── embedded/                # LZ4/tar embedded asset runtime
│   ├── command_line_parser_std.* # Qt-free CLI parser
│   ├── i18n_cli.*               # Qt-free translation system
│   ├── main_cli_cpp.cpp         # CLI entry point (Qt-free)
│   ├── helper.cpp               # Privileged allowlisted helper (Qt-free)
│   ├── *_qt_oracle.{h,cpp}      # Oracle tests (Qt vs C++ validation)
│   └── mainwindow.*, work.*     # Legacy GUI files (reference only)
│
├── src_gui_qt6_cpp_backend/     # Active Qt6 GUI (uses C++ backend)
│   ├── mainwindow.{h,cpp,ui}    # GUI implementation
│   └── main.cpp                 # GUI entry point
│
├── data/                         # Build-time sources embedded into binaries
│   ├── live-files/              # Runtime live-files tree
│   └── iso-templates/           # Unpacked ISO template sources
│       ├── iso-template/
│       └── template-initrd/
│
├── cmake/
│   └── EmbeddedAssets.cmake      # CMake integration for embedded payloads
│
├── scripts/
│   └── build_embedded_payloads.sh # Builds live_files + iso_templates payloads
│
├── tools/
│   ├── asset_pack.c             # Build-time LZ4 packer
│   └── asset_unpack.c           # Round-trip verifier / unpacker
│
├── compressor/                   # Build-time LZ4 compressor sources
├── decompressor/                 # Runtime LZ4 decompressor sources
├── third_party/microtar/         # Tar reader used by embedded assets runtime
│
├── debian/                       # Debian packaging (helper installed, no polkit)
│
├── tests/                        # Comprehensive test suite
│   └── unit_tests.cpp           # Oracle validation tests
│
├── translations/                 # Translation files
│   └── cli_parser/*.kv          # Qt-free translation files (36 locales)
│
├── BACKEND_API_REFERENCE.md     # Backend API documentation
├── CLI_QT_FREE_MIGRATION.md     # CLI migration details
├── QT6_GUI_MIGRATION.md         # GUI migration details
├── PORTING_DIFFS.md             # Behavioral differences
└── PORTING_TRANSLATIONS.md      # Translation system details
```

## build:

### Quick Start

For convenience, build scripts are provided:


# Check dependencies and system configuration
./configure.sh

# Clean all build directories
./clean.sh

# Build Qt-free CLI (recommended)
./build_cli_qtfree.sh

# Build GUI + CLI
./build_gui.sh

# Build and run tests
./build_tests.sh

# Build everything
./build_all.sh

### Prerequisites (same as "legacy" version)

- CMake 3.16+
- C++20 compiler (GCC 10+ or Clang 12+)
- Qt6 (Core, Widgets, Concurrent) - only for GUI
- Standard build tools (make/ninja)
- System tools: `mksquashfs`, `xorriso`, `lslogins`

Run `./configure.sh` to check if all dependencies are installed.

### targets

#### Qt-Free CLI (primary target)


# Configure
cmake -B build-make -DBUILD_GUI=OFF -DBUILD_CLI=ON -DBUILD_CLI_QT=OFF

# Build
cmake --build build-make -j$(nproc)

# Verify Qt-free
ldd ./build-make/iso-snapshot-cli | grep -i qt  # Should be empty

Result expected: iso-snapshot-cli binary with zero Qt dependencies.

#### GUI + CLI (Full Build)

# Configure
cmake -B build-gui -DBUILD_GUI=ON -DBUILD_CLI=ON

# Build
cmake --build build-gui -j$(nproc)

Expected: GUI (`s4-snapshot`), CLI (`iso-snapshot-cli`), and helper tools.

#### tests


# Configure
cmake -B build-tests -DBUILD_TESTS=ON

# Build
cmake --build build-tests -j$(nproc)

# Run tests
./build-tests/unit_tests


### build Options

| option | default | description |
|--------|---------|-------------|
| `BUILD_GUI` | ON | build Qt6 GUI |
| `BUILD_CLI` | ON | Build Qt-free CLI |
| `BUILD_CLI_QT` | ON | build Qt-based CLI (for comparison) |
| `BUILD_TESTS` | ON | build test suite |
| `QT_FREE_GUARDS` | ON | fail build if Qt "contamination" detected |

### Qt-Free guards for development:

When `QT_FREE_GUARDS=ON`, the build system automatically verifies:
No Qt includes in Qt-free source files and no Qt libraries linked into Qt-free binaries
---> build fails immediately if Qt contamination is detected, preventing regressions.

## usage

### CLI

# Create ISO from running system
iso-snapshot-cli --file my-system.iso

# Specify compression
iso-snapshot-cli --file my-system.iso --compression zstd

# Custom kernel
iso-snapshot-cli --file my-system.iso --kernel 6.1.0-18-amd64

# show help
iso-snapshot-cli --help

# Show version
iso-snapshot-cli --version

### GUI

# run GUI
s4-snapshot


### Helper tool

# privileged system helper (used internally)
sudo /usr/lib/s4-snapshot/helper exec <allowed-command> [args...]

The main CLI/GUI process should normally run as the logged-in user. Privileged
operations are delegated to the helper only when needed.

## Backend API

The backend exposes a clean C++ API for integration:

```cpp
#include "batchprocessing_cpp_runner.h"
#include "settings_cpp_builder.h"

// Build settings
SettingsCpp settings = SettingsCppBuilder::buildFromArgs(
    args, true, "s4-snapshot", "MX-Linux"
);

// Setup callbacks
BatchprocessingCppRunner::Callbacks cb;
cb.debug = [](const std::string &msg) { 
    std::cout << msg << std::endl; 
};
cb.critical = [](const std::string &err) { 
    std::cerr << "ERROR: " << err << std::endl; 
};

// Setup dependencies
BatchprocessingCppRunner::Dependencies deps;
deps.runWork = [](const WorkCppPlan &plan, 
                  const WorkCppExecutor::Callbacks &wcb) {
    return WorkCppExecutor::run(plan, wcb);
};

// Execute
auto result = BatchprocessingCppRunner::runFromSettings(
    settings, "s4-snapshot", cb, deps
);

if (result.aborted) {
    std::cerr << "Failed: " << result.abortReason << std::endl;
    return 1;
}
```

---> see [`BACKEND_API_REFERENCE.md`](BACKEND_API_REFERENCE.md) for complete API documentation.

## tests "Oracle" testing
this porting project uses comprehensive oracle testing to ensure byte-for-byte equivalence with the original Qt implementation:

# Run all oracle tests
./build-tests/unit_tests

# Verify specific component
./build-tests/unit_tests --gtest_filter="*WorkSetupEnv*"


Coverage:
 +60 Qt primitives validated /  +16 test suites /  command sequence validation / edge case coverage (errors, failures, boundary conditions...)

### manual testing:


# test CLI help output
./build-make/iso-snapshot-cli --help
# test version
./build-make/iso-snapshot-cli --version
# test invalid option
./build-make/iso-snapshot-cli --invalid-option
# test compression validation
./build-make/iso-snapshot-cli --compression invalid


### Integration testing:


# Create test ISO (privileged steps are delegated to the helper)
./build-make/iso-snapshot-cli --file test.iso

# Verify ISO created
ls -lh test.iso

# Check ISO integrity
xorriso -indev test.iso -toc

## translation system
runtime Translations:
CLI uses a Qt-free translation system based on key-value files:

- format: utf-8 text files (translations/cli_parser/*.kv)
- locales: 35+ languages supported (36 vendored .kv files)
- fallback: english if locale unavailable

### adding translations

obtain qtbase_XX.qm for target locale, then generate .kv file:

   ./build-tests/gen_cli_parser_kv \
       /path/to/qtbase_XX.qm \
       translations/cli_parser/XX.kv
Then, commit translations/cli_parser/XX.kv

Locale automatically available at runtime.

See [`PORTING_TRANSLATIONS.md`](PORTING_TRANSLATIONS.md) for details.

## Porting details:

Backend tools: all backend tools have been ported to pure C++:

**core**:
- `work_cpp_planner.cpp` - work method planning (setupEnv, createIso, copyNewIso, cleanup)
- `work_cpp_executor.cpp` - command execution engine
- `batchprocessing_cpp_runner.cpp` - main orchestration
- `settings_cpp_builder.cpp` - configuration management

**tools**:
- `file_cpp.cpp` - I/O operations
- `dir_cpp.cpp` - directory operations
- `string_cpp.cpp` - stringq manipulation
- `process_runner.cpp` - process execution
- `command_runner.cpp` - cmd execution
- `installed_to_live_cpp.cpp` - native C++ implementation of the `installed-to-live` workflow used by `setupEnv`

### installed-to-live C++ integration

The backend no longer depends on the external `/usr/sbin/installed-to-live` script from `s4-remaster` for the snapshot preparation path. Calls to `installed-to-live` are intercepted by the root helper and executed by `InstalledToLiveCpp`, a Qt-free C++ implementation of the subset used by S4 Snapshot.

Implemented commands include `start`, `bind=`, `empty=`, `live-files`, `general-files`, `general`, `passwd`, `version-file`, `adjtime`, `grubdefault`, `resumedisable`, `tdmnoautologin`, `sddmnoautologin`, `read-only`, `read-write`, `cleanup`, and `exclude`. This removes the runtime dependency on the script while keeping the existing planner flow and root helper elevation model.

### Privilege elevation model

The backend is designed to run the main application as the logged-in user and
request administrator privileges only for the specific operations that need
them. The Qt-free `helper` binary is the privileged boundary:

```
frontend (CLI, TUI, GUI, etc.)
    -> backend core
        -> elevation tool
            -> /usr/lib/<app>/helper exec <allowed-command> [args...]
```

The helper keeps a strict allowlist of commands and implements the native
`installed-to-live` workflow internally. It is not a general-purpose shell.

For the CLI frontend, `sudo` is the default and most portable elevation tool:

```bash
sudo /usr/lib/iso-snapshot-cli/helper exec mount --bind / /tmp/root
```

For graphical frontends, the backend intentionally does not mandate one
authentication mechanism. The UI author should pick the approach that fits the
framework and platform: `sudo`, `doas`, a framework-specific password dialog, or
another local policy mechanism. Polkit is not required by the backend.

A simple Qt-style approach is to ask for the password in the GUI and feed it to
`sudo -S` while running only the helper as root:

```cpp
QProcess proc;

proc.start("sudo", {
    "-S",
    "-p",
    "",
    "/usr/lib/s4-snapshot/helper",
    "exec",
    "mount",
    "--bind",
    "/",
    "/tmp/root"
});

if (!proc.waitForStarted()) {
    return;
}

const QString password = askPassword();
proc.write((password + "\n").toUtf8());
proc.closeWriteChannel();
proc.waitForFinished();

const int rc = proc.exitCode();
const QByteArray err = proc.readAllStandardError();
if (rc != 0 || err.contains("incorrect password") || err.contains("Sorry, try again")) {
    // Authentication failed or the privileged command failed.
}
```

The password must never be logged or stored. It should live only long enough to
feed `sudo` for the helper invocation.

### Embedded runtime assets

Live-files and ISO boot templates are embedded in the CLI and GUI binaries at compile time (~20 MiB LZ4-compressed payload). At runtime the backend:

1. Creates the normal `workDir` under the project temp prefix
2. Extracts live-files into `workDir/_embedded/live-files/` before `setupEnv`
3. Extracts ISO templates directly into `workDir/iso-template/` and the initrd workspace during `copyNewIso`
4. Cleans up `workDir/_embedded/` on exit (including `SIGINT` / `SIGTERM`)

No `data/` tree is installed on target systems by the Debian packages. The `data/` directory in the source tree is only used as **build input** for `scripts/build_embedded_payloads.sh` (invoked automatically by CMake):

```
data/
├── live-files/              # files/ + general-files/
└── iso-templates/
    ├── iso-template/
    └── template-initrd/
```

Edit those trees, then rebuild (`./build_cli_qtfree.sh` or `./build_gui.sh`) to refresh the embedded payloads.

**Manual payload rebuild** (usually unnecessary):

```bash
./scripts/build_embedded_payloads.sh
```

**validation**:
- `settings_validation_cpp.cpp` - configuration validation
- `settings_space_cpp.cpp` - disk space calculations
- `settings_exclusions_cpp.cpp` - exclusions management

All components have been validated through oracle tests ensuring byte-for-byte equivalence with Qt implementations.

### documents:

- [`CLI_QT_FREE_MIGRATION.md`](CLI_QT_FREE_MIGRATION.md) - CLI migration details
- [`QT6_GUI_MIGRATION.md`](QT6_GUI_MIGRATION.md) - GUI backend integration
- [`PORTING_DIFFS.md`](PORTING_DIFFS.md) - some intentional behavioral differences
- [`PORTING_TRANSLATIONS.md`](PORTING_TRANSLATIONS.md) - translation system migration


### Archi:

- **x86_64** - Primary target
- **i386** - Supported

### note: extending support:

To adapt for other distributions, you -may* have to:

Modify src/settings_debianver_cpp.cpp for version detection
Update src/settings.cpp for distro-specific paths
Modify src/work_cpp_planner.cpp for distro-specific commands
Test thoroughly with oracle validation

(maybe some other things, but this is the main you have to do)

### standards:

- **no Qt in backend**: Use *_cpp suffix for Qt-free implementations
- **immutable plans**: always follow plan-execute pattern
- **testing**: oracle tests for all primitives

### Testing requirements:

All changes must:
- Pass existing oracle tests
- add new oracle tests for new functionality
- maintain Qt-free status (verified by build guards)
- document behavioral changes in `PORTING_DIFFS.md`

## limitations

Debian-based only**: Currently supports Antix (not so sure by now, but I need to check) and Q4OS (for sure).
Privilege escalation required**: ISO creation needs administrator privileges for selected steps, but the main frontend should run as the logged-in user and delegate those steps to the helper.


## in case of issues:

### build issues

**Qt not found (GUI build)**:

# install Qt6 development packages (because Qt6 GUI still using Qt6 of course...)
sudo apt install qt6-base-dev qt6-tools-dev

**Missing system tools**:
# install required tools
sudo apt install squashfs-tools xorriso

### Runtime Issues

**Permission denied**:
# verify that an elevation tool can run the helper
sudo /usr/lib/iso-snapshot-cli/helper exec true

**Insufficient space**:
# check available space:
df -h /home/snapshot

# use different directory:
iso-snapshot-cli --file my-system.iso --directory /path/to/space

**Translation not found**:
# check locale
echo $LANG

# verify translation file exists
ls translations/cli_parser/$(echo $LANG | cut -d. -f1).kv

## Third-Party Libraries

This project includes the following third-party library:

### i18n_keyval
- **Location**: `libs/i18n_keyval/`
- **Purpose**: Qt-free internationalization for CLI and backend
- **License**: MIT License
- **Author**: Stefan Devai
- **Repository**: [https://github.com/stefandevai/i18n_keyval](https://github.com/stefandevai/i18n_keyval)
- **Version**: Included as source (no external dependency required)

The i18n_keyval library is included directly in the project to ensure all dependencies are self-contained. This eliminates the need for external package installation or Git submodules.

## License

Copyright (C) 2015-2025 MX Authors, Q4OS Team
This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
See [`LICENSE`](LICENSE) for full license text.

## Authors
- **Adrian** - Original implementation
- **Debian Team** - Debian integration
- **MX Linux Team** - MX-specific features
- **Q4OS Team** - Q4OS support and specific features
- **seb3773** - QT-free porting/enhancements

----

- **Q4OS**: [https://q4os.org](https://q4os.org)
