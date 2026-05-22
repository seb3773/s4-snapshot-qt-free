# Porting Differrences (differences in behavior)

 **intentional behavioral differences** between:
- the original codebase (Qt-based), and
- the current porting **0-Qt backend**.

The goal of the port was **maximum exactness**, but in rare cases a strict equivalence was not realistically achievable. In such cases I explicitly document the divergence.

---

## `projectName` treated as literal (not a regex)

- `Settings::setVariables()` / distro version normalization
- **Original behavior (Qt)**:
  - code: `distroVersion.remove(QRegularExpression("^" + projectName + "_|^" + projectName + "-"))`
  - `projectName` is injected into a regular expression **without escaping**.
  - if `projectName` contains regex metacharacters (e.g. `.`, `[`, `(`, `|`, ...), they are interpreted by the regex engine.
port behavior (0-Qt backend target):
  - `projectName` is treated as a **literal identifier**, not a regex.
  - implementation is based on literal prefix checks:
    - if `distroVersion.startsWith(projectName + "_")`, strip that prefix
    - else if `distroVersion.startsWith(projectName + "-")`, strip that prefix
input constraint added (GUI):
  - `MainWindow::setup()` now enforces a validator on `textProjectName`:
    - allowed pattern: `^[A-Za-z0-9_-]+$`
- **Why this divergence ?**:
  - `projectName` is user-controlled (GUI input) and therefore cannot be assumed to be regex-safe.
  - preserving the original (implicit) regex semantics would require keeping `QRegularExpression` in the backend or embedding a full regex engine; both contradict the “0 Qt backend” objective.
  - the validator makes the behavior explicit and prevents accidental regex injection.
files:
  - `src/settings.cpp`
  - `src/mainwindow.cpp`
  - `tests/unit_tests.cpp`
  - `PORTING.md`
test:
  - `test_projectname_prefix_stripping_defined_literal_vs_manual()`

---

---

## GUI adaptation — exclusion file path accessor changed (backend `QFile` removal)
Qt6 GUI (`mainwindow.cpp`) adapting to a backend interface change.
- **Why this change ?**:
  - during the backend cleanup of the Qt primitive **`QFile` (object)** from `Settings`, the member `Settings::snapshotExcludes` (type `QFile`) was removed. The GUI previously used `settings->snapshotExcludes.fileName()` to obtain the configured excludes file path.
  So, to keep the GUI building without reintroducing Qt-shaped compatibility API into the backend, GUI now reads the path from `Settings::snapshotExcludesPath`.
  --> None intended (path string should be identical; only accessor changed).
files:
  - `src/mainwindow.cpp`

---
