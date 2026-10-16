# Config Foundation (Config GUI sub-project 1 of 4)

Date: 2026-10-16
Status: Draft for review

## Context

Reflex Launcher should get an in-app configuration GUI, so the whole launcher can be set up from the couch without editing `config.ini` by hand. The agreed product intent covers all four sub-projects:

- A **Config** entry, shown like any other app icon, opens a settings screen covering every `config.ini` option. Each option type gets the best-fitting control: toggles for on/off, dropdowns for fixed choices, steppers/sliders for bounded numbers, a color picker for colors, a text input for free text, and file/folder browsers for paths.
- The screen is fully operable with a **remote or gamepad** (arrows + Select + Back). A physical keyboard is used for text when present, with an on-screen keyboard as the fallback.
- **Menu editing** covers all menus: add, remove, and reorder entries, and create, rename, and delete submenus. An entry can open an installed **app** (picked from a discovered list), a **website** (opened fullscreen/kiosk in the default browser), a submenu, or a special command.
- **Icons** are auto-detected (the app's own icon, or the website's icon fetched from the network), with an override from bundled icons or an image file.
- Changes are **staged**. Save writes `config.ini` (keeping a `.bak`) and reloads the launcher in place. Cancel discards them.

The work is split into sub-projects, each with its own spec, plan, and PR:

1. **Config foundation (this spec):** settings schema, a comment-preserving INI editor, save + in-process reload. No UI.
2. Settings screen and widget toolkit: the Config entry, all non-menu settings.
3. Menu editor: entries, submenus, special commands.
4. App and website picker: per-OS app discovery, icon detection, favicon fetch, kiosk browser launch.

This spec covers only sub-project 1. Its job is to give sub-projects 2 to 4 a safe, tested way to read, change, validate, and persist configuration and to apply it without restarting the process.

## Current state (facts from the code)

- `parse_config_file()` (`src/util.c`) runs inih once at startup, and `config_handler()` writes into the global `Config config` (`src/launcher.c`). Many values are stored in converted, lossy forms. For example, the screensaver idle time is stored ×1000 as ms, colors are split into RGB, and percents are kept as strings. The struct therefore can't be serialized back to INI faithfully.
- Defaults exist as generated C macros (`DEFAULT_*` in `launcher_config.h`, 84 of them, from `config/config_settings.cmake`). Setting key names come from `SETTING_*` macros.
- Choice values live in `mode_settings[]` (`src/util.c`, file-static).
- `main()` is one linear startup sequence. `cleanup()` runs only at exit and tears down SDL entirely. There is no reset or re-init path.
- The config file path is freed in `main()` right after parsing.
- Startup failures use `log_fatal()`, which exits: no default menu, default menu not found, default menu fails to load, default font missing. There are 15 `log_fatal` call sites across `launcher.c`, `image.c`, `util.c`, and `clock.c`.
- There are no automated tests in the repo.

## Design

### Approach decisions (approved)

- **A1: line-preserving INI editing.** The file is edited line by line. User comments, ordering, and unknown keys survive.
- **B1: the UI edits raw INI value strings, validated by a schema.** After saving, the existing parser and validation are the single source of truth for behavior.
- **C1: in-process reload.** Split `main()` into `launcher_load()` / `launcher_unload()`, and keep the SDL window alive across a reload.
- Add a small CTest unit-test target with no new dependencies.

### Component 1: `src/ini_doc.c` / `src/ini_doc.h` (new)

An in-memory, line-oriented model of an INI file. Every input line is kept as a node with a kind:

- `SECTION`: `[Name]`
- `KEY`: `key=value`
- `COMMENTED_KEY`: `#key=value` or `;key=value`, where the text after the comment char parses as `key=value` and the key has no spaces
- `COMMENT`: any other comment
- `BLANK`
- `CONTINUATION`: an indented line following a key (inih multiline)
- `OTHER`: anything else, kept verbatim

The model records the file's newline style (LF or CRLF, detected from the first line ending) and whether a UTF-8 BOM was present, and it writes both back unchanged. Parsing mirrors inih's default build flags. They are VERIFIED in Homebrew's `ini.h` (inih r62) and LIKELY the same for distro and vcpkg builds, which use upstream defaults:

- `INI_START_COMMENT_PREFIXES ";#"`: lines starting with `;` or `#` are comments.
- `INI_ALLOW_INLINE_COMMENTS 1` with prefix `;`: a `;` preceded by whitespace ends the value. Entry values such as `Kodi;icon.png;cmd` are unaffected because their `;` has no space before it. `ini_doc_get` returns the value up to the inline comment, the same as inih. `ini_doc_set` replaces the value and drops any trailing inline comment on that line.
- `INI_ALLOW_MULTILINE 1`: a line starting with whitespace right after a key line is a continuation that inih reports as another value for the same key. `ini_doc` keeps such lines verbatim as `CONTINUATION` nodes attached to the preceding key. `ini_doc_set` removes them when it replaces the key's value.
- `INI_ALLOW_BOM 1`: a leading UTF-8 BOM is skipped.

Key matching is case-sensitive, matching `MATCH()` (`strcmp`) in `config_handler`. Whitespace around `=` is trimmed.

Public API:

```c
typedef struct ini_doc IniDoc;

IniDoc     *ini_doc_load(const char *path);           // NULL + log_error on I/O failure
IniDoc     *ini_doc_parse(const char *text);          // from memory (tests)
char       *ini_doc_serialize(const IniDoc *doc);     // malloc'd full text
int         ini_doc_save(const IniDoc *doc, const char *path);
void        ini_doc_free(IniDoc *doc);

const char *ini_doc_get(const IniDoc *doc, const char *section, const char *key);
void        ini_doc_set(IniDoc *doc, const char *section, const char *key, const char *value);
void        ini_doc_unset(IniDoc *doc, const char *section, const char *key);

size_t      ini_doc_get_list(const IniDoc *doc, const char *section, const char *prefix, char ***values);
void        ini_doc_set_list(IniDoc *doc, const char *section, const char *prefix,
                             const char *const *values, size_t count);

size_t      ini_doc_sections(const IniDoc *doc, char ***names);
bool        ini_doc_has_section(const IniDoc *doc, const char *section);

// Keys changed via set/unset since load (for validating only what the user touched):
size_t      ini_doc_dirty_keys(const IniDoc *doc, const char ***sections, const char ***keys);
void        ini_doc_add_section(IniDoc *doc, const char *section);
void        ini_doc_remove_section(IniDoc *doc, const char *section);
void        ini_doc_rename_section(IniDoc *doc, const char *from, const char *to);
```

Semantics:

- **`get`** returns the value of the last active `KEY` matching section + key, the same "last wins" rule as `config_handler`. It returns NULL if there is none. Commented keys are not active.
- **`set`** has three cases, tried in order:
  1. If an active key exists, it replaces that value in place, keeping the key's original spelling and the line's leading whitespace.
  2. Otherwise, if a `COMMENTED_KEY` for the key exists in the section, it un-comments the last such line and sets its value.
  3. Otherwise, it appends `key=value` after the last non-blank line of the section, creating the section at the end of the file (preceded by a blank line) if needed.
- **`unset`** comments out every active instance of the key as `#key=value`, so the setting falls back to its default and the previous value stays visible to anyone hand-editing.
- **`get_list` / `set_list`** handle numbered keys `<prefix>N`, such as `Entry1..EntryN` and `Hotkey1..HotkeyN`.
  - `get_list` returns active values in file order. That is the order inih reports them and therefore the order menus are built in, not numeric order.
  - `set_list` rewrites the list as `<prefix>1..<prefix>count`. It replaces the existing list lines in place, inserts extra ones after the last existing one, and deletes surplus ones. Comment lines interleaved with the list stay at their original relative positions.
- **Section operations:**
  - `remove_section` deletes the header and every line up to the next section header.
  - `rename_section` changes only the header.
  - Updating references to the section, such as `DefaultMenu` and `:submenu` commands, is the caller's job (the menu editor, sub-project 3).
- **`save`** writes `<path>.tmp`, flushes and closes it, copies the existing `<path>` to `<path>.bak` (overwriting any previous `.bak`), then renames `<path>.tmp` onto `<path>`. The rename uses `rename()` on POSIX and `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING)` on Windows, implemented in `src/platform/`. Any step failing returns non-zero, removes the `.tmp`, and leaves `<path>` untouched.

### Component 2: `src/settings_schema.c` / `src/settings_schema.h` (new)

A static table describing every scalar setting. Menus (`EntryN`) and hotkeys (`HotkeyN`) are lists and are not in this table.

```c
typedef enum {
    SET_BOOL, SET_CHOICE, SET_INT, SET_PERCENT, SET_COLOR,
    SET_TEXT, SET_FILE, SET_DIR, SET_FONT, SET_COMMAND, SET_MENU_REF
} SettingType;

typedef struct {
    const char        *section;       // "General", "Clock", ...
    const char        *key;           // SETTING_* macro
    const char        *label;         // UI label, e.g. "Show date"
    const char        *help;          // one-line UI help text
    SettingType        type;
    int                min, max;      // SET_INT; SET_PERCENT uses 0..100 unless narrower
    const char *const *choices;       // SET_CHOICE, NULL-terminated
    const char        *default_value; // INI-format string, or NULL = "unset" (feature off / auto)
} SettingDef;

extern const SettingDef settings_schema[];
extern const size_t     settings_schema_count;

const SettingDef *schema_find(const char *section, const char *key);
size_t            schema_section_settings(const char *section, const SettingDef ***out);
bool              schema_validate(const SettingDef *def, const char *value, char *err, size_t err_len);
```

Rules:

- **Unique lookup.** Lookups always use section + key, because keys like `Enabled`, `Color`, and `Font` repeat across sections.
- **Validation reuses parser helpers.**
  - `SET_BOOL` uses `convert_bool`.
  - `SET_COLOR` uses `hex_to_color`.
  - `SET_PERCENT` uses `is_percent` plus the range check.
  - `SET_INT` uses a strict integer parse plus `min`/`max`, taken from the existing `MIN_*`/`MAX_*` macros in `launcher.h`. Any unit conversion stays in the parser: `ScreensaverIdleTime` is validated in seconds, the same unit as the file.
  - `SET_CHOICE` must match one of `choices`, which point into `mode_settings[]`. That array is made non-static and declared in `util.h` so it isn't duplicated.
  - `SET_FILE`, `SET_FONT`, and `SET_DIR` only require a non-empty value. Existence is not checked, because a missing file must not block saving; the parser already falls back and logs.
  - `SET_MENU_REF` must name an existing menu section. The check happens in `config_check`, which has the document.
  - `SET_COMMAND` and `SET_TEXT` accept any string without newlines.
- **Coverage.**
  - All 28 `SETTING_GAMEPAD_*` keys are included. `Enabled` is `SET_BOOL`, `DeviceIndex` is `SET_INT`, `MappingsFile` is `SET_FILE`, and the stick, trigger, and button bindings are `SET_COMMAND`.
  - The remaining `SETTING_*` keys cover General, Background, Layout, Titles, Highlight, Scroll Indicators, Clock, and Screensaver.
- **Defaults** are produced from the `DEFAULT_*` macros as INI strings: colors as `#RRGGBB`, bools as `true`/`false`, and choices by name.

### Component 3: `src/config_io.c` / `src/config_io.h` (new)

Glue between the document and the running launcher:

```c
bool config_check(const IniDoc *doc, char *err, size_t err_len);
int  config_save(IniDoc *doc, char *err, size_t err_len);  // validate + check + ini_doc_save
```

`config_check` catches the structural conditions that would make a reload fail or `log_fatal`. It checks that:

- `[General] DefaultMenu` is set and names an existing section
- that section has at least one entry
- each `EntryN` value has exactly three `;`-separated fields and a non-empty title and command
- every `:submenu X` command targets an existing section
- every `HotkeyN` has the `keycode;command` form

The error string names the section, key, and problem.

`config_save` validates only the **dirty** keys (from `ini_doc_dirty_keys`) with `schema_validate`. Pre-existing invalid values the user didn't touch never block a save; the parser already ignores them and keeps defaults, as today. It then calls `config_check` and `ini_doc_save(doc, config.config_path)`. It does **not** reload. The caller calls `launcher_reload()` after a successful save, so the UI can close itself first.

If `ini_doc_save` fails because the target isn't writable, the error says so and suggests the user config location (`~/.config/flex-launcher/config.ini` on POSIX, the executable's directory on Windows).

### Component 4: launcher load/unload refactor (`src/launcher.c`, `src/util.c`, `src/launcher.h`)

- **Defaults.** `Config config = { ... }` becomes `static const Config default_config = { ... }` plus `Config config;`. Loading starts with `config = default_config;`. Fields that must survive a reload are saved and restored around that assignment: `exe_path`, `config_path`, `debug`.
- **Config path.** A new field, `char *config_path`, is set from `handle_arguments`/`find_file` and no longer freed in `main()`.
- **`static int launcher_load(void)`** contains everything in today's `main()` from `parse_config_file()` through `load_menu(default_menu, ...)`, and the startup command on first load only. It runs in two stages:
  - **Window stage:** creates the window if `window == NULL` and always creates the renderer, because VSync is a renderer flag.
  - **Config stage:** parse, validate, fonts, background, slideshow, clock, highlight, scroll indicators, overlay, menus, gamepads, and on Windows the exit hotkey.

  The calls that `log_fatal` today on the load path return non-zero through `launcher_load()`: missing or unknown default menu, default menu fails to load, default font missing. They log with `log_error` when `state.reloading` is set, and keep exiting through the same `log_fatal` at startup.
- **`static void launcher_unload(void)`** frees everything `launcher_load()` created, and nothing else:
  1. waits on `Slideshowhread` and `clock_thread`, then sets both to NULL
  2. destroys all textures (entry icons and titles, background, overlay, highlight, scroll, screensaver, clock), then the renderer
  3. closes fonts and calls `quit_slideshow()` if active
  4. frees the menus, entries, hotkeys, and gamepad-control lists, and the `config` strings
  5. closes gamepads and unregisters the Windows exit hotkey

  It resets `current_menu`, `current_entry`, `default_menu`, `state`, and `ticks` as needed. It keeps the window, the SDL/IMG/TTF subsystems, nanosvg, and the log file. The existing hotkey and gamepad free loops are also fixed: they currently free the previous node only and leak the last one.
- **`int launcher_reload(void)`:**
  1. `launcher_unload()`
  2. `launcher_load()`
  3. On failure, it logs the error, copies `<config>.bak` over `<config>` (keeping the failed file as `<config>.failed`), unloads, and loads again.
  4. If that also fails, it calls `log_fatal` with a message naming both files.
- **`cleanup()`** becomes `launcher_unload()` followed by window destroy, `quit_svg()`, `TTF_Quit()`, `IMG_Quit()`, `SDL_Quit()`, and closing the log.
- **New special command `:reload`** (`SCMD_RELOAD` in `launcher.h`, handled in `execute_command`). It reloads from disk. This makes the sub-project testable before any UI exists, and helps people who hand-edit the file. It's documented in `docs/configuration.md` and noted in `CHANGELOG`.
- **Main loop.** A reload requested during event handling sets `state.reload_pending`. The reload runs at the top of the next main-loop iteration, never inside the event switch. The event queue is flushed after a reload.

### Data flow (for sub-projects 2 and 3)

```
Config screen opens -> ini_doc_load(config.config_path) -> UI reads values with
ini_doc_get + schema_find -> user edits -> ini_doc_set / ini_doc_unset / ini_doc_set_list
-> Save: config_save() -> ok -> close UI -> launcher_reload()
                        -> error -> show message, stay in UI
Cancel: ini_doc_free(), no file changes.
```

Concurrent edits: when the Config screen opens it records the file's size and mtime. `config_save` checks them again and returns a distinct "file changed on disk" error. The UI (sub-project 2) can then offer to overwrite or reload.

## Error handling summary

| Failure | Behavior |
|---|---|
| Invalid value in a staged change | `config_save` returns an error naming the field. Nothing is written. |
| Structural problem (default menu, bad entry, dangling submenu) | Same as above, via `config_check`. |
| Target not writable (e.g. packaged `/usr/share/...`) | Save fails with a message suggesting the user config location. Nothing changes. |
| Disk write fails mid-save | `.tmp` removed, `config.ini` untouched. |
| File changed on disk since the screen opened | Distinct error code; the UI decides. |
| Reload fails after a successful save | Restore `.bak`, keep the bad file as `.failed`, reload. If that fails too, `log_fatal` naming both files. |
| Startup load fails | Unchanged: `log_fatal` as today. |

## Testing

New `tests/` directory and CMake target `reflex-tests`, built when `BUILD_TESTING` is ON (`include(CTest)`) and run by `ctest`. Tests are plain C with a minimal assert macro and no new dependencies. They link `ini_doc.c`, `settings_schema.c`, `config_io.c`, and `util.c`, plus SDL2 and inih, which are already dependencies. No code is moved for testability. A test-only stub file provides the few launcher globals `util.c` references (`config`, `hotkeys`, `gamepad_controls`, `quit()`).

- **`ini_doc`:**
  - parity with inih: for a corpus including inline `;` comments, `#`/`;` comment lines, and continuation lines, `ini_doc_get` agrees with what `ini_parse_string` reports
  - parse → serialize gives byte-identical output for the shipped `config.ini`, a CRLF file, a file with a BOM, and a file with odd spacing and comments
  - `set` covers its three cases: replace, un-comment, append, including creating a section
  - `unset`
  - "last wins" behavior of `get`
  - `set_list`: grow, shrink, reorder, and interleaved comments preserved
  - section add, remove, and rename
  - `save` against a temp directory: `.bak` created, `.tmp` removed on simulated failure
- **`settings_schema`:**
  - every `SETTING_*` key in `config_settings.cmake` appears exactly once (checked against a list generated at configure time)
  - every default passes its own validation
  - each type has accept and reject cases at its boundaries
- **`config_check`:** missing or unknown `DefaultMenu`, empty default menu, malformed entry, dangling `:submenu`, malformed hotkey.
- **Parser equivalence:** for each schema setting with a non-NULL default, build a document with only that value set, run it through the real `config_handler` into a fresh `Config`, and assert it equals `default_config`. This catches drift between the schema defaults and the parser.

Manual verification, on macOS locally, with Linux and Windows build-checked via `gh workflow run Build`:

- Hand-edit `config.ini`, trigger `:reload` from a hotkey, and confirm the change shows without the window closing. Cover icon size, background color, clock toggle, VSync on/off, and changing the default menu.
- Break the default menu, `:reload`, and confirm it falls back to `.bak` and leaves `.failed`.
- Reload 50 times and confirm resident memory is stable (`leaks`/`vmmap` on macOS).
- Reload while the slideshow transition and clock thread are running: no crash.

## Out of scope

- Any UI, the Config entry, and widgets (sub-project 2).
- Menu-editing UX and updating references when menus are renamed (sub-project 3).
- App discovery, icons, websites (sub-project 4).
- Watching `config.ini` for changes automatically.
- Renaming the `flex-launcher` executable or paths.

## Open risks

- **`launcher_unload` completeness.** Any resource missed leaks on each reload. Mitigated by the 50-reload memory check and by making `cleanup()` reuse `launcher_unload()`, so exit and reload share one path.
- **Windows reload behavior** (window and renderer recreation with transparency and `SDL_SysWMinfo`) can only be build-checked here, not run. Flagged for manual testing on a Windows machine.
- **inih parity.** `ini_doc` must agree with inih on what counts as a key, a comment, and a section, or `get` could disagree with what the launcher actually loads. This is mitigated by the parity test that runs `ini_parse_string` on the same corpus. A distro or vcpkg inih built with non-default flags would still diverge (NOT VERIFIED for every platform).
