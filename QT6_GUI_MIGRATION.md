# S4 snapshot Qt6 GUI to C++ Backend migration

- current GUI code (used by default): `src_gui_qt6_cpp_backend/mainwindow.{h,cpp}` - pure C++ backend
- legacy Qt code (kept for reference): `src/mainwindow.{h,cpp}` - original Qt-based Work class architecture

The project uses the migrated version by default (cf CMakeLists.txt). The GUI uses the pure C++ backend via BatchprocessingCppRunner with asynchronous execution (QtConcurrent).

Active build (default): Uses src_gui_qt6_cpp_backend/mainwindow.cpp - C++ backend with Qt6 GUI
legacy code (reference only): src/mainwindow.cpp - original Qt-based Work class (not built by default)
backend: fully Qt-free (no Qt dependencies in backend/tools/CLI)

---

## Overview
migration of Qt6 GUI from Qt-based Work class to pure C++ backend API, for complete architectural separation, maintaining 100% functional equivalence.

## Architecture changes:

### Before 
- **persistent work instance**: `Work work` member in MainWindow
- **direct method calls**: 12 direct calls to Work methods throughout GUI
- **Qt-based callbacks**: Work class used Qt signals/slots for callbacks
- **sequential workflow**: 5 separate method calls for ISO creation workflow
- **coupling**: GUI directly dependent on Work class implementation

### After 
- **No persistent instance**: No Work member in MainWindow
- **backend API**: main workflow uses BatchprocessingCppRunner API
- **temporary instances**: utility methods create temporary Work instances when needed
- **C++ callbacks**: std::function callbacks passed to backend
- **atomic state tracking**: thread-safe atomic flags for operation state
- **unified workflow**: single backend call replaces 5 sequential calls
- **clean separation**: GUI independent of Work implementation details

---

## code changes:

### modified files:

#### src_gui_qt6_cpp_backend/mainwindow.h

-removed:
- `#include "work.h"` removed from header
- `Work work;` persistent member removed

-added:
- `#include "batchprocessing_cpp_runner.h"` 
- `#include <atomic>` 
- `#include <functional>` 
- `std::atomic<bool> m_abortRequested{false};` 
- `std::atomic<bool> m_operationInProgress{false};` 
- `void handleBackendMessage(const std::string& msg);`
- `void handleBackendLog(const std::string& log);`
- `bool shouldAbortBackend();` 



#### src_gui_qt6_cpp_backend/mainwindow.cpp

-removed:
- Work member initialization from constructor
- `work.setMessageCallback()` and `work.setMessageBoxCallback()`
- 5 Work method calls in prepareForOutput()
- `work.startTimer()` 
- `work.isDone()` check 

-added:
- 3 callback method implementations 
- backend integration in prepareForOutput() 
- temporary Work instances for installPackage/cleanUp 
- atomic flag operations for state management
- settings synchronization after backend execution

-includes added:
- `#include "work.h"`  for temporary instances
- `#include "settings_cpp_builder.h"` 
- `#include "work_cpp_executor.h"`
- `#include "work_cpp_planner.h"`


---

## Detailed changes by integration point

### constructor
before: `work(settings)`  
after No Work member initialization  
--> Removed persistent Work instance

### setConnections()
before:
```cpp
work.setMessageCallback([this](const QString &out) { processMsg(out); });
work.setMessageBoxCallback([this](BoxType box_type, const QString &title, const QString &msg) {
    processMsgBox(box_type, title, msg);
});
```
after: Removed - callbacks now passed directly to backend in prepareForOutput()  
--> Cleaner callback management, no separate setup needed


### prepareForOutput() - Main Workflow
before: 5 sequential Work method calls
```cpp
work.setupEnv();
work.checkEnoughSpace();
work.copyNewIso();
work.savePackageList(file_name);
work.createIso(file_name);
```
after: single unified backend call
```cpp
BatchprocessingCppRunner::Result result = BatchprocessingCppRunner::runFromSettings(
    cppSettings, appName, callbacks, dependencies
);
```
--> Unified workflow, better error handling (cleaner archi.)

### installPackage()
before: `work.installPackage(package)`  
after: `Work tempWork(settings); tempWork.installPackage(package);`  
-->temp instance for utility method, no persistent dependency

### cleanUp()
before: `work.cleanUp()`  
after: `Work tempWork(settings); tempWork.cleanUp();`  
-->temporary instance for cleanup, no persistent dependency

### startTimer() 
before: `work.startTimer()`  
after: `m_operationInProgress.store(true);`  
--> simpler state tracking, thread-safe atomic operation

### isDone() 
before: `!work.isDone()`  
after: `m_operationInProgress.load()`  
--> inverted logic, thread-safe atomic read

---

## Results

cleaner separation:
- GUI code independent of Work implementation
- backend can be used in CLI without GUI dependencies
- easier to test components

reduced coupling:
- no persistent Work instance in GUI
- backend API provides clean interface
- temporary instances only when needed

abstraction:
- GUI doesn't need to know Work internals
- backend handles workflow orchestration
- clear API boundaries

thread-Safe callback mechanism:
- QMetaObject::invokeMethod ensures GUI thread safety
- atomic flags for state management
- no race conditions

Workflow execution unified:
- single backend call instead of 5 sequential calls
- better error handling and recovery
- consistent state management

Error handling:
- backend returns structured result
- clear abort/error distinction
- settings synchronized after execution

State management:
- atomic flags for thread-safe state tracking
- clear operation lifecycle
- no hidden state in Work class

Maintenance:
- backend logic centralized in C++ modules
- no duplication between GUI and CLI
- easier to maintain and update

testing:
- backend can be tested independently
- GUI callbacks can be mocked
- clear test boundaries

code organization:
- clear separation: GUI vs Backend
- modular architecture

---

## comparison before vs after:

| Aspect | Before | After | Improvement |
|--------|--------|-------|-------------|
| **Architecture** | Monolithic Work class | Modular C++ backend | better separation |
| **coupling** | Tight (persistent instance) | Loose (API calls) | reduced dependency |
| **workflow** | 5 sequential calls | 1 unified call | simplified |
| **state Management** | Work class flags | Atomic flags | thread-safe |
| **callbacks** | Qt signals/slots | std::function | more flexible |
| **Error Handling** | Implicit | Explicit result | clearer |
| **testing** | Difficult (coupled) | Easy (modular) | better testable |
| **maintenance** | Complex (intertwined) | Simple (separated) | easier to maintain |
| **reusability** | GUI-only | GUI + CLI | shared backend |
| **thread Safety** | Qt-dependent | Atomic + Qt | enhanced |

- no persistent Work instance in MainWindow
- only temporary instances for specific utilities
---> clean architectural separation achieved

- main workflow uses BatchprocessingCppRunner
- single unified call replaces 5 sequential calls
- proper callback infrastructure in place

better thread safety:
- callbacks use QMetaObject::invokeMethod
- atomic flags for state management
---> no race conditions possible

---

## References

- **src_gui_qt6_cpp_backend/** - Active GUI implementation (mainwindow.{h,cpp,ui}, main.cpp) - **USED BY DEFAULT**
- **src/** - Contains:
  - **backend C++ Qt-free** (work_cpp_*, batchprocessing_cpp_*, settings_cpp_*) - **SHARED BY ALL**
  - **CLI Qt-free** (main_cli_cpp.cpp, command_line_parser_std.*) - **ACTIVE**
  - **shared utilities** (command_runner, process_runner, about, etc.) - **SHARED BY ALL**
  - **Qt oracles** (*_qt_oracle.*) - For validation/testing
  - **legacy GUI files** (mainwindow.{h,cpp,ui}, work.{h,cpp}) - Reference only, not built by default
- **_backup_gui_original/** - Historical backup for reference

## Build config:

The build system (CMakeLists.txt) uses src_gui_qt6_cpp_backend/ by default. The legacy `src/mainwindow.cpp` is not compiled unless explicitly configured.   
