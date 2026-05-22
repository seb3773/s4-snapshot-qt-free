# porting: Translations (Qt -> Qt-free backend/tools/CLI)

--> Remove any runtime dependency on Qt translation artifacts (`.qm`, `QTranslator`, `QLocale`, `QLibraryInfo`, etc.) for:

- backend
- backend tools
- CLI (`iso-snapshot-cli`)

Qt6 GUI may keep using the existing Qt translation system.


- runtime for backend/tools/CLI must not require any Qt files installed on the system (no `/usr/.../qt.../*.qm`).
- behavior/output must remain as close as possible to the Qt-based reference.
- no external dependencies are introduced (no JSON/XML libraries).


- Library: `i18n_keyval`
- Translator: `i18n::translators::basic`
- Data format: one UTF-8 `key=value` file per locale.

`i18n_keyval` is used at runtime by backend/tools/CLI. It provides:

- locale selection
- key-based lookup

## replaced:

Qt reference implementation for CLI help/error texts relies on two sources:

- application translations (`s4-snapshot_XX.qm`) for `QObject::tr()` strings in the project.
- Qt base translations (`qtbase_XX.qm`) for internal strings emitted by `QCommandLineParser` (e.g. `Usage:`, `Options:`, `Unknown option ...`).

For a Qt-free backend/tools/CLI, we must not rely on `qtbase_XX.qm` at runtime.


### Runtime (Qt-free):

-> load vendored key-value files:
  - `translations/cli_parser/<locale>.kv`
-> initialize `i18n_keyval` with the in-memory tables.
-> translate internal `QCommandLineParser`-equivalent strings via stable keys.

### dev-time generation (Qt-free tool, input may be `.qm`)
-> generate the vendored `*.kv` files offline. The generator: reads an input `.qm` file (typically `qtbase_<locale>.qm`) ; then extracts a fixed list of internal `QCommandLineParser` strings and writes `translations/cli_parser/<locale>.kv`
--> Qt-free generator using project's `QmTranslatorCpp` reader.
This will keeps runtime Qt-free while still allowing us to reuse Qt's existing translations as the oracle for output exactness.

## format

Qt translation lookup is keyed by:
- `context`
- `sourceText`
- `comment`

`i18n_keyval` is key-based, so we must map the triple into a stable string key:

 prefix: `qt|`
 separator: `|`
 fields are escaped to avoid ambiguity.

Key:
- `qt|<context>|<sourceText>|<comment>`



- Placeholder compatibility: Qt uses `%1`, `%2`, ... We must keep them verbatim in translation values and perform placeholder substitution ourselves where needed.
- UTF-8: `*.kv` files are UTF-8.
- missing locale: `i18n_keyval` throws if locale is not available; callers must handle this deterministically.


- `CommandLineParserStd` (replacement of `QCommandLineParser`) uses `i18n_keyval` to translate internal parser messages.

## How to regenerate vendored translations

1) ensure `qtbase_<locale>.qm` is available on the development machine (input only).
2) build the generator tool:
   - `cmake --build build-tests -j --target gen_cli_parser_kv`
3) run generator tool to produce `translations/cli_parser/<locale>.kv`:
   - `./build-tests/gen_cli_parser_kv "$(qtpaths --query QT_INSTALL_TRANSLATIONS)/qtbase_<locale>.qm" translations/cli_parser/<locale>.kv`
4) run unit tests to validate exact equivalence on the extracted strings:
   - `./build-tests/unit_tests`
