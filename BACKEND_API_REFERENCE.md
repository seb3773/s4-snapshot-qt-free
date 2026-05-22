# S4-Snapshot Backend C++ API Reference

The backend follows a "Plan-Execute" pattern:


user Input -> settings -> planner -> plan -> executor -> result


**Components**:
- **BatchprocessingCppRunner**: Orchestrator
- **WorkCppPlanner**: Creates execution plans
- **WorkCppExecutor**: Executes plans
- **SettingsCpp**: configuration structure


#### Plan-Execute pattern

Immutable data structures describing what to do, executors interpret and run plans.
So : 
- plans can be inspected
- separation of concerns (planning vs execution)
- reproducibility (same plan = same result)


#### Callbacks

the backend uses callbacks for:
- **Messages**: progress updates (`debug`)
- **Errors**: critical errors (`critical`)
- **UI**: message boxes (`messageBox`)

---

## Core API
- BatchprocessingCppRunner
High-level orchestrator for complete ISO creation workflow
Main Entry Point:

```cpp
[[nodiscard]] static Result runFromSettings(
    SettingsCpp settings,
    const std::string &applicationName,
    const Callbacks &cb,
    const Dependencies &deps
);
```

Parameters:
- `settings`: configuration (see [SettingsCpp](#42-settingscpp))
- `applicationName`: application name (e.g., "s4-snapshot")
- `cb`: callbacks for messages and errors
- `deps`: dependencies (work executor)

Returns: `Result` with abort status and updated settings

**Example**:
```cpp
SettingsCpp settings = SettingsCppBuilder::buildFromArgs(args, true, "s4-snapshot", "s4");

BatchprocessingCppRunner::Callbacks cb;
cb.debug = [](const std::string &msg) { std::cout << msg << std::endl; };
cb.critical = [](const std::string &err) { std::cerr << "ERROR: " << err << std::endl; };

BatchprocessingCppRunner::Dependencies deps;
deps.runWork = [](const WorkCppPlan &plan, const WorkCppExecutor::Callbacks &wcb) {
    return WorkCppExecutor::run(plan, wcb);
};

BatchprocessingCppRunner::Result result = BatchprocessingCppRunner::runFromSettings(
    settings, "s4-snapshot", cb, deps
);

if (result.aborted) {
    std::cerr << "Failed: " << result.abortReason << std::endl;
    return 1;
}
```


#### Structures

##### callbacks

```cpp
struct Callbacks {
    std::function<void(const std::string &text)> debug;      // Progress messages
    std::function<void(const std::string &text)> critical;   // Error messages
};
```

##### dependencies

```cpp
struct Dependencies {
    std::function<WorkCppExecutor::Result(
        const WorkCppPlan &plan,
        const WorkCppExecutor::Callbacks &cb
    )> runWork;
};
```

##### Result

```cpp
struct Result {
    bool aborted = false;           // true if workflow failed
    std::string abortReason;        // Reason for failure
    SettingsCpp settings;           // Updated settings (workDir, tempDirParent, etc.)
};
```

---

### WorkCppPlanner
Creates execution plans for each workflow stage
Setup Environment:

```cpp
[[nodiscard]] static WorkCppPlan planSetupEnv(
    const SettingsCpp &settings,
    const SetupEnvEnv &env
);
```

(prepare work directory, bind-root overlay, install dependencies)

Parameters:
- `settings`: Configuration
- `env`: Environment flags (see [SetupEnvEnv](#43-environment-structures))
Returns: Plan for setup stage

#### Create ISO

```cpp
[[nodiscard]] static WorkCppPlan planCreateIso(
    const SettingsCpp &settings,
    const std::string &filename,
    const CreateIsoEnv &env
);
```

create squashfs and ISO file

parameters:
- `settings`: configuration
- `filename`: output ISO filename
- `env`: environment flags (see CreateIsoEnv in environment-structures)

Returns: plan for ISO creation

#### Copy New ISO

```cpp
[[nodiscard]] static WorkCppPlan planCopyNewIso(
    const SettingsCpp &settings,
    const CopyNewIsoEnv &env
);
```

Copy ISO template and configure boot files

Parameters:
- `settings`: Configuration
- `env`: Environment flags (see [CopyNewIsoEnv](#43-environment-structures))

Returns: Plan for ISO template copy


#### Save Package List

```cpp
[[nodiscard]] static WorkCppPlan planSavePackageList(
    const SettingsCpp &settings,
    const std::string &fileName
);
```

Save installed packages list

Parameters:
- `settings`: configuration
- `fileName`: output filename

Returns: plan for package list save

#### Edit Boot Menu

```cpp
[[nodiscard]] static WorkCppPlan planEditBootMenu(
    const SettingsCpp &settings,
    const std::string &editorCmd
);
```

Open boot menu files in editor

Parameters:
- `settings`: Configuration
- `editorCmd`: Editor command (e.g., "gedit")

Returns: Plan for boot menu editing

#### Cleanup

```cpp
[[nodiscard]] static WorkCppPlan planCleanup(
    const SettingsCpp &settings,
    const CleanupEnv &env
);
```

Clean up temporary files and unmount overlays

parameters:
- `settings`: Configuration
- `env`: Environment flags (see [CleanupEnv](#43-environment-structures))

Returns: plan for cleanup

---

### WorkCppExecutor

Executes plans created by WorkCppPlanner

#### Execute Plan

```cpp
[[nodiscard]] static Result run(
    const WorkCppPlan &plan,
    const Callbacks &cb
);
```

Parameters:
- `plan`: execution plan from WorkCppPlanner
- `cb`: callbacks for messages and UI

returns: `Result` with abort status

Example:
```cpp
WorkCppPlan plan = WorkCppPlanner::planSetupEnv(settings, env);

WorkCppExecutor::Callbacks cb;
cb.message = [](const std::string &msg) { std::cout << msg << std::endl; };
cb.messageBox = [](BoxType type, const std::string &title, const std::string &text) {
    std::cerr << title << ": " << text << std::endl;
};

WorkCppExecutor::Result result = WorkCppExecutor::run(plan, cb);

if (result.aborted) {
    std::cerr << "Execution failed: " << result.abortReason << std::endl;
}
```

#### Structures

##### Callbacks

```cpp
struct Callbacks {
    std::function<void(const std::string &text)> message;
    std::function<void(BoxType type, const std::string &title, const std::string &text)> messageBox;
};
```

##### result

```cpp
struct Result {
    bool aborted = false;
    std::string abortReason;
};
```

---

## High-Level API

### Complete workflow:

```cpp
// 1. Build settings from arguments
SettingsCpp settings = SettingsCppBuilder::buildFromArgs(args, true, "s4-snapshot", "s4");

// 2. Setup callbacks
BatchprocessingCppRunner::Callbacks cb;
cb.debug = [](const std::string &msg) { /* log message */ };
cb.critical = [](const std::string &err) { /* log error */ };

// 3. Setup dependencies
BatchprocessingCppRunner::Dependencies deps;
deps.runWork = [](const WorkCppPlan &plan, const WorkCppExecutor::Callbacks &wcb) {
    return WorkCppExecutor::run(plan, wcb);
};

// 4. Run complete workflow
BatchprocessingCppRunner::Result result = BatchprocessingCppRunner::runFromSettings(
    settings, "s4-snapshot", cb, deps
);

// 5. Check result
if (result.aborted) {
    std::cerr << "Failed: " << result.abortReason << std::endl;
    return 1;
}

std::cout << "ISO created successfully!" << std::endl;
return 0;
```

---

## Data Structures

### WorkCppPlan

Immutable execution plan

structure:
```cpp
struct WorkCppPlan {
    std::vector<WorkCppPlanStep> steps;
};
```

step types**:
- `Debug`: debug message
- `Critical`: critical error
- `Abort`: abort with reason
- `CallWorkPlan`: execute sub-plan

### SettingsCpp

Configuration for ISO creation

Key fields:
```cpp
struct SettingsCpp {
    // System
    bool x86 = false;                    // 32-bit architecture
    std::uint32_t maxCores = 0;          // Max CPU cores
    
    // Directories
    std::string workDir;                 // Work directory
    std::string snapshotDir;             // Snapshot output directory
    std::string snapshotName;            // ISO filename
    std::string tempDirParent;           // Temp directory parent
    
    // Space
    std::uint64_t freeSpace = 0;         // Free space (KiB)
    std::uint64_t freeSpaceWork = 0;     // Free space in work dir (KiB)
    
    // Exclusions
    std::string snapshotExcludesPath;    // Exclusion file path
    std::string sessionExcludes;         // Runtime exclusions
    std::string excludesSourcePath;      // Source exclusion file
    
    // Compression
    std::string kernel;                  // Kernel version
    std::string compression;             // Compression format (gzip, zstd, etc.)
    std::uint32_t cores = 0;             // Cores for mksquashfs
    std::uint32_t throttle = 0;          // CPU throttle (0-100)
    std::string mksqOpt;                 // Extra mksquashfs options
    
    // Checksums
    bool makeMd5sum = false;             // Generate MD5
    bool makeSha512sum = false;          // Generate SHA512
    
    // Boot
    std::string bootOptions;             // Boot options
    std::string projectName;             // Project name
    std::string distroVersion;           // Distribution version
    std::string codename;                // Codename
    std::string fullDistroName;          // Full distribution name
    std::string releaseDate;             // Release date
    
    // Flags
    bool shutdown = false;               // Shutdown after completion
    bool resetAccounts = false;          // Reset user accounts
    bool live = false;                   // Running in live environment
    bool editBootMenu = false;           // Edit boot menu
    bool monthly = false;                // Monthly snapshot
    bool overrideSize = false;           // Override size check
};
```

### Environment Structures

#### SetupEnvEnv

```cpp
struct SetupEnvEnv {
    bool workDirContainsS4Snapshot = true;
    bool bootIsMountpoint = false;
    bool bindRootOverlayActive = false;
    bool needInstallCalamares = false;
    bool setupBindRootOverlayOk = true;
    bool setupBindRootOverlay_bindRootIsMountpoint = false;
    bool setupBindRootOverlay_lowerIsMountpoint = false;
    bool setupBindRootOverlay_bindMountOk = true;
    bool setupBindRootOverlay_overlayMountOk = true;
    std::string applicationName;
    std::string elevateTool;
    bool mxVersionFileExistsInUsrLocal = true;
    bool lsbReleaseExistsInUsrLocal = true;
    bool cleanUp_started = true;
    bool cleanUp_done = false;
    bool cleanUp_cleanupConfExists = false;
    bool cleanUp_bindRootOverlayBaseNonEmpty = false;
};
```

#### CreateIsoEnv

```cpp
struct CreateIsoEnv {
    bool useUnbuffer = false;
    std::string umaskOut;
    std::string applicationName;
    std::string elevateTool;
    int debianVerNum = 0;
    std::string bindRootPath = "/run/iso-snapshot-cli/bind-root-overlay/root";
};
```

#### CopyNewIsoEnv

```cpp
struct CopyNewIsoEnv {
    bool isoTemplateMultiExists = false;
    bool sysvinitInitExists = false;
    bool systemdSystemdExists = false;
    bool initrdReleaseExists = false;
    bool initrdReleaseIsFile = false;
    bool initrdReleaseDestExists = false;
    bool initrd_releaseExists = false;
    bool initrd_releaseIsFile = false;
    bool initrd_releaseDestExists = false;
    bool initrdTempDirValid = true;
    std::string initrdTempDirPath;
    std::string loggedInUserName;
    std::string applicationName;
};
```

#### CleanupEnv

```cpp
struct CleanupEnv {
    bool started = true;
    bool done = false;
    bool cleanupConfExists = false;
    bool bindRootOverlayBaseNonEmpty = false;
    bool shutdownRequested = false;
    std::string applicationName;
    std::string elevateTool;
    std::string snapshotDir;
    std::string snapshotName;
};
```

-----------------------------------------------------------------------

#### All Functions

##### BatchprocessingCppRunner

```cpp
[[nodiscard]] static Result run(
    const BatchprocessingCppPlan &plan,
    const SettingsCpp &settings,
    const Callbacks &cb,
    const Dependencies &deps
);

[[nodiscard]] static Result runFromSettings(
    SettingsCpp settings,
    const std::string &applicationName,
    const Callbacks &cb,
    const Dependencies &deps
);
```

##### WorkCppPlanner

```cpp
[[nodiscard]] static WorkCppPlan planSetupEnv(
    const SettingsCpp &settings,
    const SetupEnvEnv &env
);

[[nodiscard]] static WorkCppPlan planCreateIso(
    const SettingsCpp &settings,
    const std::string &filename,
    const CreateIsoEnv &env
);

[[nodiscard]] static WorkCppPlan planCopyNewIso(
    const SettingsCpp &settings,
    const CopyNewIsoEnv &env
);

[[nodis
card]] static WorkCppPlan planSavePackageList(
    const SettingsCpp &settings,
    const std::string &fileName
);

[[nodiscard]] static WorkCppPlan planEditBootMenu(
    const SettingsCpp &settings,
    const std::string &editorCmd
);
```

##### WorkCppExecutor

```cpp
[[nodiscard]] static Result run(
    const WorkCppPlan &plan,
    const Callbacks &cb
);
```

#### All Structures:

##### SettingsCpp (53 fields)

##### SetupEnvEnv (18 fields)

##### CreateIsoEnv (5 fields)

##### CopyNewIsoEnv (13 fields)

##### CleanupEnv (9 fields)




#### All Enums

##### BoxType

```cpp
enum class BoxType {
    question,      // Question dialog (yes/no)
    information,   // Information dialog (OK)
    warning,       // Warning dialog (OK)
    critical       // Critical error dialog (OK)
};
```



### Command reference

#### Special commands

The executor recognizes these special commands in `RunCommandLine` steps:

##### WRITE_TEXT_FILE_UTF8_TRUNCATE

format: `WRITE_TEXT_FILE_UTF8_TRUNCATE <path> <text>`

(Write text to a file (UTF-8, no BOM, truncate if exists))

Example:
```cpp
plan_run_cmd(p, "WRITE_TEXT_FILE_UTF8_TRUNCATE /tmp/test.txt Hello World", false);
```

##### WRITE_LINUXFS_INFO_FROM_MKSQUASHFS_OUTPUT

format: `WRITE_LINUXFS_INFO_FROM_MKSQUASHFS_OUTPUT <path>`

(Extract filesystem info from mksquashfs output and write to file)

example:
```cpp
plan_run_cmd(p, "WRITE_LINUXFS_INFO_FROM_MKSQUASHFS_OUTPUT /tmp/linuxfs.info", false);
```

** Uses output from the last `ProcAsRoot` command (typically mksquashfs).

##### CHECK_RESULT

format: `CHECK_RESULT installed-to-live <success_msg> else ERROR: <error_msg>`

(Check if the last command succeeded, show error and cleanup if failed)

ex:
```cpp
plan_run_cmd(p, "CHECK_RESULT installed-to-live Success else ERROR: Installation failed", false);
```

--> If last `ProcAsRoot` command failed, shows error message box and performs cleanup.

##### REPLACE_MENU_STRINGS

format: `REPLACE_MENU_STRINGS <workDir>|<projectName>|<distroVersion>|<fullDistroName>|<releaseDate>|<codename>|<bootOptions>`

(Replace placeholder strings in ISO boot menu files)

ex:
```cpp
plan_run_cmd(p, "REPLACE_MENU_STRINGS /tmp/work|MX Linux|23.4|MX-23.4_x64|2024-01-01|Libretto|quiet splash", false);
```

files modified:
- `<workDir>/iso-template/antiX/boot/grub/grub.cfg`
- `<workDir>/iso-template/antiX/boot/isolinux/isolinux.cfg`


##### MD5_CHECKSUM

format: `MD5_CHECKSUM <folder> <filename>`

(Calculate MD5 checksum and save to `<filename>.md5`)

ex:
```cpp
plan_run_cmd(p, "MD5_CHECKSUM /home/snapshot test.iso", false);
```

--> Output: creates `/home/snapshot/test.iso.md5`

##### SHA512_CHECKSUM

format: `SHA512_CHECKSUM <folder> <filename>`

(Calculate SHA512 checksum and save to `<filename>.sha512`)

example:
```cpp
plan_run_cmd(p, "SHA512_CHECKSUM /home/snapshot test.iso", false);
```

--> output: creates `/home/snapshot/test.iso.sha512`

#### Standard commands

All other commands in `RunCommandLine` steps are executed as shell commands:

```cpp
plan_run_cmd(p, "mkdir -p /tmp/test", false);
plan_run_cmd(p, "cp /etc/fstab /tmp/fstab.bak", false);
plan_run_cmd(p, "echo 'Hello' > /tmp/hello.txt", false);
```

### Issues


##### "Compression format not supported"

 `abortReason == "checkCompression failed"`

-->  Selected compression format is not supported by the running kernel

  ->  Check kernel config: `grep CONFIG_SQUASHFS /boot/config-$(uname -r)`
  -> select a different compression format (gzip is always supported)
  -> Or use `--compression gzip` CLI option

ex:
```bash
# Check what's supported
grep CONFIG_SQUASHFS /boot/config-$(uname -r)

# Use gzip (always supported)
iso-snapshot-cli --compression gzip
```



##### "Not enough disk space"
 `abortReason == "checkEnoughSpace failed"`
= Insufficient disk space for ISO creation
-> free up disk space
-> use `--override-size` to skip space check (risky)
-> change snapshot directory to a partition with more space
**Example**:
# check available space
df -h /home/snapshot
# use different directory
iso-snapshot-cli --directory /mnt/large-partition/snapshot
# or override (not a good idea)
iso-snapshot-cli --override-size





#####"Snapshot directory validation failed"
We have :  `abortReason == "snapshot/temp dir check failed"`
--> cannot create or access snapshot directory
We have to:
-> check directory permissions
-> ensure parent directory exists
-> check if running with sufficient privileges
**Example**:
# check perms
ls -ld /home/snapshot
# create directory
sudo mkdir -p /home/snapshot
sudo chown $USER: /home/snapshot




##### command execution failed:
 `abortReason` contains "command failed", so a system command failed during execution
-> Check system logs for details
-> Ensure needed tools are installed (mksquashfs, xorriso, etc.)
-> Check for system resource issues (disk full, out of memory)

**Example**:
# required tools are installed ?
which mksquashfs xorriso
# if missing, let's install (Debian/debian based)
sudo apt-get install squashfs-tools xorriso
# check system resources to be sure
df -h
free -h




##### thread safety violation:
Occurs if we update UI from worker thread without proper synchronization
So we have to use `QMetaObject::invokeMethod` with `Qt::QueuedConnection`
**Example**:
```cpp
// **this is wrong**
cb.debug = [this](const std::string &msg) {
    ui->label->setText(QString::fromStdString(msg));  // NOT THREAD-SAFE!
};

// This is **CORRECT**
cb.debug = [this](const std::string &msg) {
    QMetaObject::invokeMethod(this, [this, msg]() {
        ui->label->setText(QString::fromStdString(msg));
    }, Qt::QueuedConnection);
};
```



#### Debugging

##### verbose log:

```cpp
BatchprocessingCppRunner::Callbacks cb;
cb.debug = [](const std::string &msg) {
    std::cout << "[DEBUG] " << msg << std::endl;
    // Also log to file
    std::ofstream log("/tmp/iso-creation.log", std::ios::app);
    log << "[DEBUG] " << msg << std::endl;
};
cb.critical = [](const std::string &msg) {
    std::cerr << "[ERROR] " << msg << std::endl;
    std::ofstream log("/tmp/iso-creation.log", std::ios::app);
    log << "[ERROR] " << msg << std::endl;
};
```

##### Inspect plans before execution:

```cpp
WorkCppPlan plan = WorkCppPlanner::planCreateIso(settings, filename, env);
// Inspect plan steps
std::cout << "Plan has " << plan.steps.size() << " steps:" << std::endl;
for (size_t i = 0; i < plan.steps.size(); ++i) {
    const auto &step = plan.steps[i];
    if (std::holds_alternative<WorkCppPlanStep::Message>(step.payload)) {
        std::cout << "  [" << i << "] Message: " 
                  << std::get<WorkCppPlanStep::Message>(step.payload).text << std::endl;
    } else if (std::holds_alternative<WorkCppPlanStep::RunCommandLine>(step.payload)) {
        std::cout << "  [" << i << "] Command: "
                  << std::get<WorkCppPlanStep::RunCommandLine>(step.payload).command << std::endl;
    }
    // ... inspect other step types ...
}
// Execute plan
WorkCppExecutor::Result result = WorkCppExecutor::run(plan, cb);
```

##### Check environment probes:

```cpp
// Log environment state before planning
std::cout << "Environment state:" << std::endl;
std::cout << "  isoTemplateMultiExists: " << env.isoTemplateMultiExists << std::endl;
std::cout << "  sysvinitInitExists: " << env.sysvinitInitExists << std::endl;
std::cout << "  systemdSystemdExists: " << env.systemdSystemdExists << std::endl;
// ... log other environment fields ...
WorkCppPlan plan = WorkCppPlanner::planCopyNewIso(settings, env);
```

##### Validate settings:

```cpp
// Validate settings before running
std::cout << "Settings:" << std::endl;
std::cout << "  snapshotDir: " << settings.snapshotDir << std::endl;
std::cout << "  snapshotName: " << settings.snapshotName << std::endl;
std::cout << "  compression: " << settings.compression << std::endl;
std::cout << "  cores: " << settings.cores << std::endl;
std::cout << "  workDir: " << settings.workDir << std::endl;
// Check for common issues
if (settings.snapshotDir.empty()) {
    std::cerr << "ERROR: snapshotDir is empty!" << std::endl;
}
if (settings.snapshotName.empty()) {
    std::cerr << "ERROR: snapshotName is empty!" << std::endl;
}
if (settings.cores == 0) {
    std::cerr << "WARNING: cores is 0, will use default" << std::endl;
}
```

#### tips & infos

**customizing ISO boot menu**
Use the `--edit-boot-menu` option. The backend will pause and open the boot menu in text editor: make changes, save, and close the editor to continue.


**root privileges**
ISO creation requires root privileges for:
- Creating bind-root overlay
- Running mksquashfs
- Setting file ownership
- Installing Calamares (if needed)


**adding custom files to the ISO**
place files in the work directory after `copyNewIso` but before `createIso`. 
--> The backend doesn't provide a direct API for this; we need to modify the plan or add files manually between steps.
  
  
**difference between `iso-snapshot-cli` and `iso-snapshot-cli-qt`?**
- `iso-snapshot-cli`: Pure C++ CLI, no Qt dependencies, smaller binary, faster startup
- `iso-snapshot-cli-qt`: Qt-based CLI, uses Qt for argument parsing and some utilities, larger binary
both produce identical ISOs
  
  
**use backend in a web application ?**
- Backend must run on the server (requires root privileges)
- Use callbacks to send progress to web client (WebSocket, SSE, etc.)
- Implement proper security (authentication, authorization, rate limiting)
- Consider resource limits (one ISO creation at a time)
  
---
  
  
### Summary
  
 **Zero Qt Dependencies**: Backend completely Qt-free  
 **Functionally Equivalent**: Byte-for-byte verified against Qt implementation: ok
 **Architecture**: Separation of planning and execution  
 **Flexible**: Works with any UI framework (Qt, GTK, CLI, web, etc.)  


----------------------------------------------------------------------------------------------------------------------------------------------
- **Source Code**: `src/batchprocessing_cpp_*.{h,cpp}`, `src/work_cpp_*.{h,cpp}`
- **Tests**: `tests/unit_tests.cpp`
- **Reference Implementation**: `src/main.cpp` (CLI_BUILD section)

- Exécution de `mksquashfs` pour créer `linuxfs`
- Remplacement des strings dans les menus de boot

#### Étape 6: Sauvegarde Liste Paquets

```cpp
// Sauvegarde la liste des paquets installés
