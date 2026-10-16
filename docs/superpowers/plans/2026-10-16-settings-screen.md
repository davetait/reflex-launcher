# Settings Screen Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** An in-app Settings screen, opened by `:config`, for editing every scalar `config.ini` setting with a remote, gamepad or keyboard. Changes are staged, and Save writes the file and reloads the launcher.

**Architecture:** Four testable, display-free models:
- `settings_model`: values, stepping, set/reset, save.
- `text_input`: the on-screen keyboard.
- `file_browser`: directory navigation.
- `ui_controller`: modes and actions.

Two SDL layers sit on top:
- `ui_render`, with a text-texture cache. It is checked visually by a snapshot tool that writes PNGs.
- `config_ui`, which maps events to actions and hooks into the launcher.

Everything edits `config.ini` through the foundation from PR #7 (`ini_doc`, `settings_schema`, `config_io`).

**Tech Stack:** C11, SDL2, SDL2_ttf, SDL2_image, inih, CMake + CTest. No new dependencies.

**Spec:** `docs/superpowers/specs/2026-10-16-settings-screen-design.md`

## Global Constraints

- **Code style:** C11; 4-space indent; `snake_case` functions; `PascalCase` typedefs; `static` file-local helpers with prototypes at the top of each file. Must compile under MSVC (VS2022 traditional preprocessor), GCC, and Clang. No GNU extensions; no variadic macro forwarding.
- **Platform guards:** `#ifdef PLATFORM_POSIX` for Linux/macOS (never `__unix__`), `#ifdef _WIN32` for Windows.
- **Config access:** all reads and writes of `config.ini` go through `ini_doc`, `settings_schema` and `config_io`. Every `ini_doc` edit invalidates pointers previously returned by `ini_doc_get`/`ini_doc_get_list`/`ini_doc_sections`, so **copy values before any edit**.
- **Reloads:** the UI never calls `launcher_reload()`. After a successful save it closes and calls `launcher_request_reload()`.
- **Sections:** listed in `schema_sections[]` order (9 sections). Hotkeys and menus are not editable here.
- **Fixed input mapping (spec):**
  - Arrows/D-pad/left stick: move.
  - Enter/A: select.
  - Escape/B (and Backspace outside text entry): back.
  - Delete/Y: reset.
  - Ctrl+S/F2/Start: save.
  - Backspace/X in text entry: erase.
  - Launcher hotkeys, gamepad bindings and the screensaver are suspended while the screen is open.
- **Theme and fonts:** the UI uses its own font (bundled `OpenSans-Regular.ttf`, found the same way `load_font` finds the default), sized from the screen height, with a fixed theme. It never uses the user's title or clock font or colors.
- **Tests:** under `-DBUILD_TESTING=ON` only. Release CI is unaffected.
- **Commits:** end with `-m "Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"`. No push from task implementers.

## Review Focus

1. **A setting whose current value is invalid** (e.g. `Opacity=lots`, `IconSize=5000`). It shows as invalid without crashing. Stepping starts from the default (or the range minimum) and Reset clears it. *(Task 1 test `test_invalid_existing_value`)*
2. **Settings opened on a read-only `config.ini`** (packaged `/usr/share/...`). Save shows the "Could not write … copy it to …" message and the screen stays open with the edits intact. *(Task 4 test `test_save_write_failure_keeps_screen`)*
3. **Very long values and non-ASCII text** in the keyboard. Backspace removes one whole UTF-8 character, the cursor moves by characters, and the 199-byte line limit is reported by Save, not by a crash. *(Task 2 test `test_utf8_erase_and_cursor`, Task 4 test `test_save_too_long_value`)*
4. **The file browser on a folder that is missing or unreadable.** It falls back to the next start candidate, and if every candidate fails it shows a message instead of an empty or crashing list. *(Task 3 test `test_open_falls_back_and_fails_cleanly`)*
5. **Leaving with unsaved changes** through Back on the section list. It must never silently discard. Discard only happens through the explicit dialog choice. *(Task 4 test `test_back_with_changes_asks`)*

---

## File Structure

| File | Status | Responsibility |
|---|---|---|
| `src/config_ui/settings_model.{h,c}` | new | Owns the `IniDoc` and config path. Display value per setting, step left/right, set/reset with validation, save. |
| `src/config_ui/text_input.{h,c}` | new | On-screen keyboard model: layouts, key focus, UTF-8 text buffer and cursor. |
| `src/config_ui/file_browser.{h,c}` | new | Directory listing model with filters, navigation and picking. |
| `src/config_ui/ui_controller.{h,c}` | new | `UiState` plus `ui_apply()`. Mode machine for sections, rows, choice list, text, files, color, dialogs and messages. |
| `src/config_ui/ui_render.{h,c}` | new | Draws a `UiState` with SDL. Theme, layout, widgets. |
| `src/config_ui/ui_text.{h,c}` | new | LRU cache of text textures keyed by (font, color, text). |
| `src/config_ui/config_ui.{h,c}` | new | Glue: open/close, SDL event → `UiAction`, stick repeat, draw call. |
| `src/platform/fileops.h`, `unix.c`, `win32.c` | modify | `list_directory()` / `free_dir_entries()`, `home_directory()`. |
| `src/image.{h,c}` | modify | Extract `char *find_default_font(const char *name)` from `load_font()`. |
| `src/launcher.{h,c}` | modify | `SCMD_CONFIG ":config"`. Route events and drawing to the UI while it is open; suspend hotkeys, gamepad bindings and the screensaver. |
| `src/CMakeLists.txt`, `tests/CMakeLists.txt` | modify | New sources. Test suites `settings_model`, `text_input`, `file_browser`, `ui_controller`. Target `reflex-ui-snapshot`. |
| `tests/test_settings_model.c`, `test_text_input.c`, `test_file_browser.c`, `test_ui_controller.c`, `tests/ui_snapshot.c` | new | Tests and the snapshot tool. |
| `assets/icons/settings.svg`, `config/config.ini.in` | new/modify | Settings icon and sample menu entry. |
| `docs/configuration.md`, `README.md`, `CHANGELOG`, `CLAUDE.md` | modify | Docs. |

Build/test commands (repo root):
```bash
cmake -B build-tests -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DEXTRA_WARNINGS=ON
cmake --build build-tests --target reflex-tests && ctest --test-dir build-tests --output-on-failure
```
New suites are registered by adding the name to `TEST_SUITES` and a `{name, fn}` row to `suites[]` in `tests/test_main.c`, exactly like the existing three.

---

### Task 1: Settings model

**Files:**
- Create: `src/config_ui/settings_model.h`, `src/config_ui/settings_model.c`, `tests/test_settings_model.c`
- Modify: `tests/CMakeLists.txt`, `tests/test_main.c`, `src/CMakeLists.txt`

**Interfaces:**
- Consumes: `ini_doc_*`, `settings_schema[]`, `schema_sections[]`, `schema_find`, `schema_section_settings`, `schema_validate`, `config_save`, `ConfigSaveResult` (PR #7).
- Produces:
```c
typedef struct settings_model SettingsModel;
typedef enum { VALUE_SET, VALUE_DEFAULT, VALUE_NOT_SET, VALUE_INVALID } ValueState;

SettingsModel *settings_model_open(const char *config_path, char *err, size_t err_len); // NULL + err if unreadable
SettingsModel *settings_model_open_text(const char *text);                                // tests; path NULL
void settings_model_close(SettingsModel *m);
size_t settings_model_section_count(void);                        // 9
const char *settings_model_section_name(size_t index);            // schema_sections[index]
size_t settings_model_row_count(size_t section);
const SettingDef *settings_model_row(size_t section, size_t row);
ValueState settings_model_display(const SettingsModel *m, const SettingDef *def, char *buf, size_t len);
bool settings_model_step(SettingsModel *m, const SettingDef *def, int direction, char *err, size_t err_len);
bool settings_model_set(SettingsModel *m, const SettingDef *def, const char *value, char *err, size_t err_len);
void settings_model_reset(SettingsModel *m, const SettingDef *def);
size_t settings_model_choices(const SettingsModel *m, const SettingDef *def, char ***out); // malloc'd copies; caller frees each + array
bool settings_model_dirty(const SettingsModel *m);
const char *settings_model_path(const SettingsModel *m);
ConfigSaveResult settings_model_save(SettingsModel *m, bool force, char *err, size_t err_len);
```

**Behavior to implement:**

`settings_model_display` writes into `buf` according to the value's state:

| State | Condition | `buf` contents |
|---|---|---|
| `VALUE_SET` | active value is valid | the value |
| `VALUE_INVALID` | active value fails `schema_validate` | the raw value |
| `VALUE_DEFAULT` | unset, `def->default_value` non-NULL | the default |
| `VALUE_NOT_SET` | unset, no default | empty string |

`SET_MENU_REF` is valid only if the named section exists and is a menu.

**Step base.** The value stepping starts from is the active value if valid, else `default_value` if non-NULL, else none. "None" applies to INT, PERCENT, LENGTH and DECIMAL; the first step then sets `min`, or `"0%"`/`"0"`, regardless of direction.

Step rules, all clamped, with `direction` = -1 or +1:

| Type | Rule |
|---|---|
| `SET_BOOL` | toggles `"true"`/`"false"`. With no base, sets `"true"`. |
| `SET_CHOICE` / `SET_MENU_REF` | next or previous in `settings_model_choices` order, wrapping. With no base, the first choice. |
| `SET_INT` | step = 1 if (max−min) ≤ 100; 10 if ≤ 1000; else 100. |
| `SET_PERCENT` | ±5 within [min, max], formatted `"%d%%"`. |
| `SET_LENGTH` | if the base ends in `%`, ±5 within [0, 100] as `"N%"`; else ±5 within [0, max] as `"N"`. |
| `SET_DECIMAL` | ±0.5 within [min, max], formatted with `%g`, so `1.5`, `3`, `0.5`. |
| other types | return false with err `"Select this setting to edit it"`. |

For `SET_MENU_REF`, `settings_model_choices` returns the doc's sections that are not in `schema_sections[]` and are not `"Hotkeys"`, in file order. For `SET_CHOICE` it returns a copy of `def->choices`.

`settings_model_set` runs `schema_validate`. For `MENU_REF` it also requires the section to be a menu. Then it calls `ini_doc_set`, and if that returns false the err is `"This value can't be stored in config.ini"`. It never changes the doc on failure. `settings_model_reset` calls `ini_doc_unset`.

`settings_model_dirty` is true when `ini_doc_dirty_count > 0`. `settings_model_save` calls `config_save(doc, path, force, …)`. A model opened with `settings_model_open_text` has no path: its save returns `CONFIG_SAVE_WRITE_FAILED` with err `"No config file to save to"`, without calling `config_save`.

- [ ] **Step 1: Write the failing tests** in `tests/test_settings_model.c`, as `suite_settings_model()`:

```c
static const char *sample =
    "[General]\nDefaultMenu=Main\nVSync=true\n#FPSLimit=\nOnLaunch=Blank\n"
    "[Layout]\nIconSize=256\nIconSpacing=5%\nVCenter=50%\n"
    "[Background]\nSlideshowTransitionTime=3\n"
    "[Clock]\nOpacity=lots\n"
    "[Main]\nEntry1=A;a.png;cmd\n[System]\nEntry1=S;s.png;:sleep\n";

static void test_sections_and_rows(void)          // 9 sections, schema order; Clock has 13 rows; row(0,0) is DefaultMenu
static void test_display_states(void)
    // VSync -> VALUE_SET "true"; FPSLimit -> VALUE_NOT_SET ""; ShowDate ([Clock]) -> VALUE_DEFAULT "false"
static void test_step_bool_choice_wraps(void)
    // VSync right -> "false"; OnLaunch right x3 from "Blank" -> "None","Quit","Blank"; left from "Blank" -> "Quit"
static void test_step_numbers(void)
    // IconSize 256 right -> "266" (range 992 => step 10); IconSize "1024" right stays "1024"
    // FPSLimit (unset, no default) right -> "11" (MIN_FPS_LIMIT+1)
    // VCenter "50%" left -> "45%"; VCenter "25%" left stays "25%"
    // IconSpacing "5%" right -> "10%"; IconSpacing "40" right -> "45"
    // SlideshowTransitionTime "3" left -> "2.5"
static void test_step_menu_ref(void)              // DefaultMenu "Main" right -> "System" -> "Main"; choices == {"Main","System"}
static void test_invalid_existing_value(void)
    // [Clock] Opacity "lots" -> VALUE_INVALID "lots"; step right -> "100%" (default base "100%", clamped); reset -> VALUE_DEFAULT
static void test_set_validates(void)
    // set IconSize "5000" -> false, err non-empty, value still "256"; set StartupCmd "a ; b" -> false ("can't be stored");
    // set DefaultMenu "Layout" -> false; set BackgroundColor "#112233" -> true, dirty == true
static void test_step_unsupported_type(void)      // step on [Background] Color -> false, err "Select this setting to edit it"
```

Use `settings_model_open_text(sample)` for all except a save round-trip test:

```c
static void test_save_round_trip(void)
    // write sample to tmp file; open; set VSync false; save(force=false) == CONFIG_SAVE_OK; file contains "VSync=false"; dirty == false
```

- [ ] **Step 2: Run to verify it fails.** Run the build/test commands. Expected: build error (`settings_model.h` missing).

- [ ] **Step 3: Implement** `src/config_ui/settings_model.c` per the behavior above. The struct holds `IniDoc *doc` and `char *path`. Copy any `ini_doc_get` string into a local buffer before calling a setter. Add both files to `src/CMakeLists.txt` `SOURCES`, and add them to `TEST_SOURCES`/`TEST_SUITES` in `tests/CMakeLists.txt`.

- [ ] **Step 4: Run the tests.** Expected: all suites pass, including `settings_model`.

- [ ] **Step 5: Commit** `git commit -m "Settings model: display, step, set/reset, save" -m "Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"`

---

### Task 2: On-screen keyboard model

**Files:**
- Create: `src/config_ui/text_input.h`, `src/config_ui/text_input.c`, `tests/test_text_input.c`
- Modify: `tests/CMakeLists.txt`, `tests/test_main.c`, `src/CMakeLists.txt`

**Interfaces:**
- Produces:
```c
typedef enum { LAYOUT_TEXT, LAYOUT_NUMBER } TextLayout;
typedef enum { TEXT_NONE, TEXT_DONE, TEXT_CANCEL } TextResult;
typedef struct text_input TextInput;

TextInput *text_input_new(const char *initial, TextLayout layout); // cursor at end, focus on first key
void text_input_free(TextInput *t);
const char *text_input_value(const TextInput *t);
size_t text_input_cursor(const TextInput *t);                       // byte offset
void text_input_move_focus(TextInput *t, int dx, int dy);           // clamps; changing rows clamps column
TextResult text_input_press(TextInput *t);                          // activates focused key
void text_input_type(TextInput *t, const char *utf8);               // physical typing at cursor
void text_input_erase(TextInput *t);                                // delete one UTF-8 char before cursor
void text_input_move_cursor(TextInput *t, int delta);               // by characters
size_t text_input_rows(const TextInput *t);
size_t text_input_row_length(const TextInput *t, size_t row);
const char *text_input_key_label(const TextInput *t, size_t row, size_t col);
void text_input_focus(const TextInput *t, size_t *row, size_t *col);
```

**Layouts.** The text value is capped at 511 bytes; typing past that is ignored.

`LAYOUT_TEXT` has three pages. Key labels are exactly as written here.
- Page "abc":
  - rows `1234567890`, `qwertyuiop`, `asdfghjkl`, `zxcvbnm`
  - special row `Shift`, `#+=`, `Space`, `⌫`, `←`, `→`, `Done`, `Cancel`
- Page "ABC" (after Shift): the same with uppercase letters. Shift applies to one character, then reverts to lowercase.
- Page "#+=":
  - rows `1234567890`, `!@#$%^&*()`, `-_=+[]{}\|`, `;:'",.<>/?`, `` `~ ``
  - special row `abc`, `Space`, `⌫`, `←`, `→`, `Done`, `Cancel`

`LAYOUT_NUMBER` rows: `789`, `456`, `123`, `-0.`, `%`, `⌫`, `←`, `→`, `Done`, `Cancel`.

Pressing a character key inserts it at the cursor. `⌫` erases, `←`/`→` move the cursor, `Done` returns `TEXT_DONE`, `Cancel` returns `TEXT_CANCEL`, and `Shift`/`#+=`/`abc` switch pages. Focus resets to (0, 0) on a page switch.

- [ ] **Step 1: Write the failing tests**, as `suite_text_input()`:

```c
static void test_initial_state(void)      // value "abc", cursor 3, focus (0,0), key_label(0,0) == "1"
static void test_press_inserts_and_moves(void)
    // new(""); move_focus(0,1) -> row 1 "q"; press -> "q"; move_focus(3,0) -> "r"; press -> "qr"
static void test_shift_one_shot(void)     // focus Shift (row 4 col 0), press, then press row1 col0 -> "Q", next press -> "q"
static void test_done_and_cancel(void)    // focus "Done" press -> TEXT_DONE; "Cancel" -> TEXT_CANCEL; value unchanged by them
static void test_symbols_page(void)       // press "#+=" -> key_label(1,0) == "!"; press "abc" -> key_label(1,0) == "q"
static void test_number_layout(void)      // rows 5; key_label(0,0)=="7"; key_label(3,2)=="."; typing via press builds "7.5"
static void test_utf8_erase_and_cursor(void)
    // new("añb"); move_cursor(-1) -> cursor 3 (after "añ", 'ñ' is 2 bytes); erase -> "ab", cursor 1
    // type("é") -> "aéb"; move_cursor(+5) clamps to end (4)
static void test_max_length(void)         // type 600 'x' -> strlen(value) == 511
static void test_focus_clamps_between_rows(void) // focus row1 col9 ("p"), move_focus(0,1) -> row 2 col 8 ("l")
```

- [ ] **Step 2: Run to verify it fails.** Expected: build error.
- [ ] **Step 3: Implement** `text_input.c`. Layout tables are static arrays of UTF-8 label strings per page; the special row is the last row of each page.
- [ ] **Step 4: Run the tests.** Expected: all pass.
- [ ] **Step 5: Commit** `"On-screen keyboard model"` (+ attribution line).

---

### Task 3: File browser model + directory listing

**Files:**
- Create: `src/config_ui/file_browser.h`, `src/config_ui/file_browser.c`, `tests/test_file_browser.c`
- Modify: `src/platform/fileops.h`, `src/platform/unix.c`, `src/platform/win32.c`, `tests/CMakeLists.txt`, `tests/test_main.c`, `src/CMakeLists.txt`

**Interfaces:**
- Produces (fileops.h):
```c
typedef struct { char *name; bool is_dir; } DirEntry;
int list_directory(const char *path, DirEntry **entries, size_t *count); // 0 ok; skips "." ".." and names starting with '.'; is_dir follows symlinks
void free_dir_entries(DirEntry *entries, size_t count);
const char *home_directory(void);                                        // $HOME or %USERPROFILE%, may be NULL
```
POSIX uses `opendir`/`readdir` + `stat`. Win32 uses `FindFirstFileA`/`FindNextFileA` on `path\*`.
- Produces (file_browser.h):
```c
typedef enum { FILTER_IMAGES, FILTER_FONTS, FILTER_DIRS, FILTER_ANY } FileFilter;
typedef enum { ENTRY_USE_FOLDER, ENTRY_PARENT, ENTRY_DIR, ENTRY_FILE } EntryKind;
typedef struct { const char *name; EntryKind kind; } FileEntry;
typedef enum { FB_NAVIGATED, FB_CHOSEN, FB_ERROR } FileBrowserResult;
typedef struct file_browser FileBrowser;

FileBrowser *file_browser_open(const char *const *candidates, size_t candidate_count, FileFilter filter,
                               char *err, size_t err_len);   // first candidate that lists; a file path uses its folder
void file_browser_free(FileBrowser *fb);
const char *file_browser_dir(const FileBrowser *fb);
size_t file_browser_count(const FileBrowser *fb);
FileEntry file_browser_entry(const FileBrowser *fb, size_t index);
FileBrowserResult file_browser_activate(FileBrowser *fb, size_t index, char *out_path, size_t out_len,
                                        char *err, size_t err_len);
```

**Filters** (extensions case-insensitive):
- `FILTER_IMAGES`: `.png .jpg .jpeg .webp .svg .bmp`
- `FILTER_FONTS`: `.ttf .otf`
- `FILTER_DIRS`: folders only
- `FILTER_ANY`: all files

**Entry order:**
1. `ENTRY_USE_FOLDER` ("Use this folder"), only for `FILTER_DIRS`.
2. `ENTRY_PARENT` (".."), unless the folder is the filesystem root.
3. Folders, case-insensitive sorted.
4. Matching files, case-insensitive sorted.

**Activate:**
- `ENTRY_PARENT` or a folder: navigate and return `FB_NAVIGATED`. If the new folder can't be listed, stay put and return `FB_ERROR` with err `"Can't open <path>"`.
- A file, or `USE_FOLDER`: write the full path to `out_path` and return `FB_CHOSEN`.
- Paths are joined with `PATH_SEPARATOR`.

- [ ] **Step 1: Write the failing tests**, as `suite_file_browser()`. Build a temp tree under `TEST_TMP_DIR/fb` containing:
  - `Pics/`
  - `b.png`
  - `A.JPG`
  - `notes.txt`
  - `.hidden.png`
  - `fonts/x.ttf`

```c
static void test_lists_sorted_filtered(void)
    // open {root} FILTER_IMAGES -> entries: "..", "fonts", "Pics", "A.JPG", "b.png" (count 5; notes/.hidden excluded)
static void test_dirs_filter_has_use_folder(void)   // FILTER_DIRS -> entry0 ENTRY_USE_FOLDER, then "..", "fonts", "Pics"; activate(0) -> FB_CHOSEN path == root
static void test_navigate_and_choose(void)
    // FILTER_FONTS: activate "fonts" -> FB_NAVIGATED, dir ends with "fonts"; activate "x.ttf" -> FB_CHOSEN, path ends with "fonts/x.ttf" (PATH_SEPARATOR)
    // activate ".." -> back to root
static void test_file_candidate_uses_its_folder(void) // candidates {root"/b.png"} -> dir == root
static void test_open_falls_back_and_fails_cleanly(void)
    // candidates {"/no/such", root} -> dir == root; candidates {"/no/such"} -> NULL, err non-empty
```

- [ ] **Step 2: Run to verify it fails.**
- [ ] **Step 3: Implement** `list_directory`/`free_dir_entries`/`home_directory` in `unix.c` and `win32.c` (declared in `fileops.h`), then `file_browser.c`. Keep the root check portable: `"/"` on POSIX, `X:\` on Windows.
- [ ] **Step 4: Run the tests.** Expected: all pass. The app still builds (`cmake --build build`).
- [ ] **Step 5: Commit** `"File browser model and directory listing"` (+ attribution).

---

### Task 4: UI controller (modes and actions)

**Files:**
- Create: `src/config_ui/ui_controller.h`, `src/config_ui/ui_controller.c`, `tests/test_ui_controller.c`
- Modify: `tests/CMakeLists.txt`, `tests/test_main.c`, `src/CMakeLists.txt`

**Interfaces:**
- Consumes: Tasks 1–3.
- Produces:
```c
typedef enum { UI_UP, UI_DOWN, UI_LEFT, UI_RIGHT, UI_SELECT, UI_BACK, UI_RESET, UI_SAVE, UI_ERASE, UI_ACCEPT } UiAction;
typedef enum { UI_RESULT_NONE, UI_RESULT_CLOSE, UI_RESULT_SAVED } UiResult;
typedef enum { MODE_SECTIONS, MODE_ROWS, MODE_CHOICE_LIST, MODE_TEXT, MODE_FILES, MODE_COLOR,
               MODE_CONFIRM_EXIT, MODE_CONFIRM_OVERWRITE, MODE_MESSAGE } UiMode;
#define UI_PALETTE_SIZE 16
extern const char *const ui_palette[UI_PALETTE_SIZE];

typedef struct {
    SettingsModel *model;
    UiMode mode;
    size_t section, row;            // focus
    size_t list_index;              // choice list, dialog button, color cell (0..16; 16 = "Custom hex…")
    char **choices; size_t choice_count;
    TextInput *text; FileBrowser *files;
    const SettingDef *editing;      // setting being edited in a sub-mode
    UiMode return_mode;             // mode a MESSAGE returns to
    bool close_after_message;
    char message[512];              // dialog text (MESSAGE, CONFIRM_*)
    char footer[256];               // transient footer error, cleared on next move
} UiState;

void ui_init(UiState *s, SettingsModel *model);       // MODE_SECTIONS, focus 0
void ui_init_error(UiState *s, const char *message);  // MODE_MESSAGE, close_after_message = true
void ui_free(UiState *s);                             // frees model, text, files, choices
UiResult ui_apply(UiState *s, UiAction a);
void ui_type(UiState *s, const char *utf8);           // physical typing; only in MODE_TEXT
const char *const *ui_dialog_buttons(const UiState *s, size_t *count);
```

`ui_palette` is, in order: `#FFFFFF #C0C0C0 #808080 #000000 #FF3B30 #FF9500 #FFCC00 #34C759 #00C7BE #30B0C7 #007AFF #5856D6 #AF52DE #FF2D55 #A2845E #1C1C1E`.

Dialog buttons:
- `MODE_CONFIRM_EXIT`: `Save`, `Discard`, `Keep editing`.
- `MODE_CONFIRM_OVERWRITE`: `Overwrite`, `Cancel`.
- `MODE_MESSAGE`: `OK`.

**Mode rules.** Any move clears `footer`.

`MODE_SECTIONS`:
- UP/DOWN moves the section, clamped.
- RIGHT/SELECT goes to `MODE_ROWS` with row 0.
- BACK: if the model is dirty, go to `CONFIRM_EXIT`; otherwise return `CLOSE`.
- SAVE: save (see below).

`MODE_ROWS`:
- UP/DOWN moves the row, clamped. LEFT at row focus returns to `SECTIONS` **only for non-steppable types**. For steppable types, LEFT/RIGHT call `settings_model_step`, and a failure puts its err in `footer`.
- BACK returns to `SECTIONS`. RESET calls `settings_model_reset`. SAVE saves.
- SELECT depends on the type:

| Type | SELECT does |
|---|---|
| BOOL | step +1 |
| CHOICE, MENU_REF | `MODE_CHOICE_LIST`, with `list_index` = the current choice |
| INT, PERCENT, LENGTH, DECIMAL | `MODE_TEXT` with `LAYOUT_NUMBER`, prefilled with the displayed value |
| TEXT, COMMAND | `MODE_TEXT` with `LAYOUT_TEXT` |
| COLOR | `MODE_COLOR`, with `list_index` = the palette index matching the current value, else 16 |
| FILE, FONT, DIR | `MODE_FILES`; filter is IMAGES for FILE except `ControllerMappingsFile` (ANY); FONTS for FONT; DIRS for DIR |

File browser start candidates: the current value, then the config file's folder, then `home_directory()`. If they all fail, show `MESSAGE` with the err.

`MODE_CHOICE_LIST`: UP/DOWN moves, SELECT sets the value and returns to ROWS, BACK returns to ROWS unchanged.

`MODE_TEXT`:
- Arrows call `text_input_move_focus`.
- SELECT calls `text_input_press`. On `TEXT_DONE` or ACCEPT, set the value. On failure, stay in TEXT with `footer` = err. On success, return to ROWS.
- `TEXT_CANCEL` or BACK returns to ROWS unchanged.
- ERASE calls `text_input_erase`.

`MODE_COLOR`:
- Arrows move in a 4-column grid plus a final "Custom hex…" cell (index 16), reached by DOWN from the bottom row.
- SELECT on a swatch sets that color.
- SELECT on 16 goes to `MODE_TEXT` (`LAYOUT_TEXT`), prefilled with the current hex.
- BACK returns to ROWS.

`MODE_FILES`: UP/DOWN moves `list_index`. SELECT calls `file_browser_activate`:
- `FB_CHOSEN`: set the value and return to ROWS.
- `FB_NAVIGATED`: `list_index` = 0.
- `FB_ERROR`: put the err in `footer`.

BACK returns to ROWS.

**Save** (SAVE action, or the `Save` button) runs `settings_model_save(force=false)`:
- OK: return `UI_RESULT_SAVED`.
- `CHANGED_ON_DISK`: go to `CONFIRM_OVERWRITE`. `Overwrite` saves with force=true; `Cancel` returns.
- `INVALID` or `WRITE_FAILED`: go to `MODE_MESSAGE` with the err, then OK returns to the previous mode.

`MODE_CONFIRM_EXIT`:
- LEFT/RIGHT/UP/DOWN move `list_index` (0–2).
- SELECT: `Save` saves, `Discard` returns `CLOSE`, `Keep editing` returns to SECTIONS.
- BACK equals `Keep editing`.

`MODE_MESSAGE`: SELECT/BACK return to `return_mode`, or return `CLOSE` if `close_after_message`.

- [ ] **Step 1: Write the failing tests**, as `suite_ui_controller()`. Use `settings_model_open_text` (sample from Task 1) unless a file is needed.

```c
static void test_section_navigation(void)        // DOWN x20 clamps at 8; RIGHT -> MODE_ROWS row 0; BACK -> MODE_SECTIONS
static void test_back_clean_closes(void)          // BACK at sections with no edits -> UI_RESULT_CLOSE
static void test_back_with_changes_asks(void)
    // edit VSync (rows, SELECT); BACK; BACK -> MODE_CONFIRM_EXIT; buttons {"Save","Discard","Keep editing"};
    // BACK -> MODE_SECTIONS (keep editing); BACK again -> CONFIRM_EXIT; RIGHT, SELECT ("Discard") -> UI_RESULT_CLOSE
static void test_bool_select_toggles(void)        // General row "VSync": SELECT -> value "false"
static void test_left_right_step(void)            // Layout IconSize RIGHT -> "266"; on [Background] Color LEFT -> MODE_SECTIONS
static void test_choice_list(void)                // General OnLaunch SELECT -> MODE_CHOICE_LIST list_index 0 ("Blank"); DOWN, SELECT -> "None", MODE_ROWS
static void test_text_edit_done_and_invalid(void)
    // Layout IconSize SELECT -> MODE_TEXT (number layout) prefilled "256"; ERASE x3; ui_type "5000"; ACCEPT -> stays MODE_TEXT, footer non-empty
    // ERASE x4; ui_type "128"; ACCEPT -> MODE_ROWS, value "128"
static void test_color_picker(void)
    // Background Color SELECT -> MODE_COLOR list_index 3 ("#000000"); RIGHT, SELECT -> "#FF3B30"
    // SELECT again, DOWN x4 -> index 16; SELECT -> MODE_TEXT prefilled "#FF3B30"
static void test_reset(void)                      // VSync RESET -> display VALUE_DEFAULT
static void test_save_success(void)               // tmp file; edit; SAVE -> UI_RESULT_SAVED
static void test_save_write_failure_keeps_screen(void)
    // model opened from a tmp file, then the file's directory made read-only (chmod 0500, POSIX only) or path pointed at a missing dir;
    // SAVE -> MODE_MESSAGE, message contains "Could not write"; SELECT -> back to previous mode; dirty still true
static void test_save_too_long_value(void)
    // StartupCmd via text: type 250 'x', ACCEPT (stored); SAVE -> MODE_MESSAGE containing "too long"
static void test_overwrite_flow(void)
    // tmp file; edit; rewrite file externally (different size); SAVE -> MODE_CONFIRM_OVERWRITE; SELECT ("Overwrite") -> UI_RESULT_SAVED
static void test_error_screen_closes(void)        // ui_init_error("x"); SELECT -> UI_RESULT_CLOSE
```

- [ ] **Step 2: Run to verify it fails.**
- [ ] **Step 3: Implement** `ui_controller.c` per the mode rules.
- [ ] **Step 4: Run the tests.** Expected: all pass.
- [ ] **Step 5: Commit** `"Settings UI controller"` (+ attribution).

---

### Task 5: Rendering, text cache, snapshot tool

**Files:**
- Create: `src/config_ui/ui_text.{h,c}`, `src/config_ui/ui_render.{h,c}`, `tests/ui_snapshot.c`
- Modify: `src/image.c`, `src/image.h` (extract `find_default_font`), `src/CMakeLists.txt`, `tests/CMakeLists.txt` (new executable target `reflex-ui-snapshot`, not a CTest suite)

**Interfaces:**
- Consumes: Task 4 `UiState`, `ui_dialog_buttons`, `ui_palette`; the Task 1–3 getters.
- Produces:
```c
// image.h
char *find_default_font(const char *filename);  // malloc'd path or NULL; same search as load_font's fallback

// ui_text.h
typedef enum { UI_FONT_BODY, UI_FONT_SMALL, UI_FONT_HEADER } UiFont;
typedef struct ui_text_cache UiTextCache;
UiTextCache *ui_text_cache_new(SDL_Renderer *r, int screen_height); // opens OpenSans at h/27 (body), h/36 (small), h/18 (header); NULL if no font
void ui_text_cache_free(UiTextCache *c);
SDL_Texture *ui_text(UiTextCache *c, UiFont f, const char *text, SDL_Color color, int *w, int *h); // cached, LRU 256

// ui_render.h
void ui_render(SDL_Renderer *r, UiTextCache *c, const UiState *s, int w, int h);
```

**Layout** (in screen fractions of w×h; scale everything from these):

| Element | Spec |
|---|---|
| Panel | full screen, rgba(14,17,23,235) |
| Margin | m = 0.05·h |
| Header | "Settings" in `UI_FONT_HEADER` at (m, m) |
| Sidebar | x = m, y = m + 0.10·h, width 0.24·w. One item per section, height 1.9×body line. The focused item gets a rounded accent fill rgba(61,139,253,255) when focus is in SECTIONS, or an outline in other modes. |
| Rows area | x = m + 0.27·w, right edge w − m, same top. Row height 2.0×body line. Label left in rgba(230,233,238,255); control right-aligned. The focused row has a fill rgba(255,255,255,28) plus a 3px accent left bar. Visible rows = what fits above the footer. The first visible row is `row < visible ? 0 : row − visible + 1`. |
| Footer | y = h − m − 2×small line. Line 1: the focused row's help, or `footer` in rgba(255,99,99,255) if non-empty. Line 2: hints `Select: change   ←/→: adjust   Y/Del: reset   Start/Ctrl+S: save   B/Esc: back`. |

Controls (`settings_model_display`):

| Control | Drawing |
|---|---|
| BOOL | pill 2.4×1.2 body-line. Accent when true, rgba(90,96,110,255) when false; white knob. |
| CHOICE / MENU_REF / INT / DECIMAL | `‹  value  ›` |
| PERCENT | 0.18·w bar with accent fill to the value fraction, then "N%" text |
| LENGTH | `‹ 5% ›` or `‹ 40 px ›` |
| COLOR | swatch square (1.2 body-line) with a 1px white outline, then the hex |
| TEXT / COMMAND / FILE / DIR / FONT | the value; paths over 40 characters show "…" + the last 39 |

Unset values are shown as `Default: X` in rgba(140,146,160,255), or `Not set`. `VALUE_INVALID` shows the raw value in the footer color with the suffix `  (invalid)`.

Overlays (centered card 0.5·w wide, rgba(28,33,43,250), radius 12) are drawn over the rows:
- `CHOICE_LIST`: a vertical list with the focused item filled.
- `TEXT`: the field showing the value with a cursor bar at `text_input_cursor`, then the key grid laid out from `text_input_rows`/`row_length`/`key_label`, with the focused key filled in accent.
- `FILES`: the current folder path (shortened from the left), then the entries. Folders get a trailing `/`, and "Use this folder" is in accent.
- `COLOR`: a 4×4 swatch grid plus a "Custom hex…" button, with the focused cell outlined white 3px.
- `CONFIRM_*` / `MESSAGE`: the message (wrapped to the card width), then the buttons in a row, the focused one filled.

Rounded rects can be drawn as filled rects plus corner circles, or with `SDL_RenderGeometry` (SDL ≥ 2.0.18). Choose `SDL_RenderGeometry` only if the version check passes; otherwise use rects. The minimum is SDL 2.0.14, so provide the rect fallback.

**Snapshot tool** `reflex-ui-snapshot <config.ini> <outdir>`:
1. `TTF_Init`/`IMG_Init`, then `SDL_CreateRGBSurfaceWithFormat(1920×1080, ARGB8888)` + `SDL_CreateSoftwareRenderer`.
2. Open the model, drive a `UiState` through actions, and after each state `ui_render` + `IMG_SavePNG`.
3. Files to write:
   - `01-sections.png`
   - `02-general.png` (ROWS on General)
   - `03-background.png` (ROWS on Background, showing a color and a choice)
   - `04-choice-list.png`
   - `05-text.png` (TEXT on StartupCmd)
   - `06-number.png` (TEXT number layout on IconSize)
   - `07-color.png`
   - `08-files.png` (FILES on Titles Font)
   - `09-confirm-exit.png`
   - `10-message.png` (`ui_init_error`)
   - `11-general-720p.png` (1280×720)
4. Use the generated `build-tests/config.ini` as input. Exit non-zero on any SDL failure.

- [ ] **Step 1: Write the snapshot tool first** (`tests/ui_snapshot.c` + target). Build it. Expected: link errors (`ui_render` undefined).
- [ ] **Step 2: Implement** `find_default_font` (refactor `load_font` to call it, with no behavior change), `ui_text.c` and `ui_render.c`.
- [ ] **Step 3: Generate snapshots.** `./build-tests/tests/reflex-ui-snapshot build-tests/config.ini .context/ui-snapshots` (adjust the path to the target's output dir). Expected: 11 PNGs, exit 0.
- [ ] **Step 4: Self-check visually.** Open each PNG and fix overlaps, clipped text, unreadable contrast, controls running off the right edge, and the 720p layout. List any remaining issues in the report.
- [ ] **Step 5: Run unit tests** (unchanged suites still pass) and the app build. Then **commit** `"Settings UI rendering and snapshot tool"` (+ attribution). Do not commit `.context/`.

---

### Task 6: Launcher integration (`:config`, input routing, icon)

**Files:**
- Create: `src/config_ui/config_ui.{h,c}`, `assets/icons/settings.svg`
- Modify: `src/launcher.c`, `src/launcher.h`, `config/config.ini.in`, `src/CMakeLists.txt`

**Interfaces:**
- Consumes: Tasks 1–5; `launcher_request_reload()`; `config.config_path`; the `renderer`, `geo.screen_width`/`geo.screen_height` globals.
- Produces:
```c
bool config_ui_is_open(void);
void config_ui_open(void);                       // opens model from config.config_path; on failure ui_init_error with the err
void config_ui_close(void);                      // frees state + text cache, SDL_StopTextInput
void config_ui_handle_event(const SDL_Event *e); // maps and applies; SAVED -> close + launcher_request_reload(); CLOSE -> close
void config_ui_update(Uint32 now);               // left-stick repeat: 400 ms initial delay, 80 ms repeat, deadzone GAMEPAD_DEADZONE
void config_ui_draw(void);                       // ui_render over the current frame
```

**Event mapping** (from the spec table):

| Input | Action |
|---|---|
| `SDL_KEYDOWN` arrows | `UI_UP`/`UI_DOWN`/`UI_LEFT`/`UI_RIGHT` |
| `Return`/`KP_Enter` | `UI_ACCEPT` in `MODE_TEXT`, else `UI_SELECT` |
| `Escape` | `UI_BACK` |
| `Backspace` | `UI_ERASE` in `MODE_TEXT`, else `UI_BACK` |
| `Delete` | `UI_RESET` |
| `F2`, or `S` with Ctrl | `UI_SAVE` |
| `SDL_TEXTINPUT` | `ui_type` (only in `MODE_TEXT`; ignore if Ctrl is held) |
| `SDL_CONTROLLERBUTTONDOWN` D-pad | arrows |
| A | `SELECT` |
| B | `BACK` |
| X | `ERASE` |
| Y | `RESET` |
| Start | `SAVE` |
| `SDL_CONTROLLERAXISMOTION` (left stick) | via `config_ui_update` repeat |

Call `SDL_StartTextInput()` on entering `MODE_TEXT` and `SDL_StopTextInput()` on leaving it.

**launcher.c changes:**
- `#define SCMD_CONFIG ":config"`. `execute_command` calls `config_ui_open()`.
- In the event loop, while `config_ui_is_open()`, forward `SDL_KEYDOWN`, `SDL_TEXTINPUT`, `SDL_CONTROLLERBUTTONDOWN` and `SDL_CONTROLLERAXISMOTION` to `config_ui_handle_event`. Skip `handle_keypress`, mouse select, hotkeys and `poll_gamepad`. `SDL_QUIT` and window and device events keep their existing handling.
- While open:
  - Skip `update_screensaver` and keep `ticks.last_input = ticks.main`.
  - Call `config_ui_update(ticks.main)`.
  - `draw_screen()` draws the background (and overlay) only, then `config_ui_draw()`, then presents.
- `launcher_unload()` calls `config_ui_close()` if open. The text cache owns textures from the renderer that is about to be destroyed.

**Sample config and icon:**
- `config/config.ini.in` `[@DEFAULT_MENU@]` gets `Entry5=Settings;@ICONS_PREFIX@settings.svg;:config`.
- `assets/icons/settings.svg`: a 256×256 viewBox gear on a circle, matching the flat circular style of `system.png`. Use a solid circle background #4A90D9 with a white gear (8 teeth, center hole). It must rasterize with nanosvg/SDL_image: plain `<circle>`/`<path>`, no filters, no CSS.

- [ ] **Step 1: Implement** `config_ui.c` and the launcher hooks above. Add the sources to `src/CMakeLists.txt`.
- [ ] **Step 2: Build with warnings.** `cmake -B build -DCMAKE_BUILD_TYPE=Debug -DEXTRA_WARNINGS=ON && cmake --build build`. Expected: no new warnings in touched files.
- [ ] **Step 3: Unit tests.** All suites still pass.
- [ ] **Step 4: Automated smoke** (keypresses can't be scripted on this Mac; keep fullscreen runs ≤ 6 s, then kill):
  1. Copy `build/config.ini` to `.context/settings-test.ini` and set `StartupCmd=:config`.
  2. Run `./build/flex-launcher -d -c .context/settings-test.ini &`, sleep 4, and check the process is alive.
  3. Check the log has no errors after "Begin program loop", then kill it.
  4. Confirm `settings.svg` loads: no "Could not load image" in the log, with the Settings entry in the default menu.
- [ ] **Step 5: Commit** `"Open Settings with :config; route input while open; settings icon"` (+ attribution).

---

### Task 7: Docs

**Files:**
- Modify: `docs/configuration.md`, `README.md`, `CHANGELOG`, `CLAUDE.md`

- [ ] **Step 1: Write the docs.**
  - **`docs/configuration.md`:** add a `#### :config` special-command entry (same style as `:reload`). It opens the in-app Settings screen, and the entry shows the input table from the spec. Add a short "Settings screen" subsection: Save reloads in place; menus and hotkeys are still edited in the file; existing installs add `EntryN=Settings;<path>/settings.svg;:config`.
  - **`README.md`:** a "Settings" bullet or short paragraph in About or Usage.
  - **`CHANGELOG` (Unreleased):** "Add in-app Settings screen (`:config`) for all scalar settings".
  - **`CLAUDE.md` Architecture:** one bullet for `src/config_ui/` (models vs render/glue, the snapshot tool command), plus the rule "UI never calls launcher_reload; uses launcher_request_reload".
- [ ] **Step 2: Verify.** Every key, button and command named in the docs exists in the code (grep `SCMD_CONFIG`, the key names in `config_ui.c`).
- [ ] **Step 3: Commit** `"Document the Settings screen"` (+ attribution).
