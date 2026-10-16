# Config Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the launcher a tested way to read, change, validate, and save `config.ini` without losing comments, and to apply a saved config without restarting the process.

**Architecture:**
- **`ini_doc`:** a new line-preserving INI document that mirrors inih's parsing rules.
- **`settings_schema`:** a static table describing all 93 scalar settings, used for validation and (later) the UI.
- **`config_io`:** structural checks and an atomic save.
- **`launcher.c`:** split into `launcher_load()` / `launcher_unload()` so a new `:reload` command can rebuild everything except the SDL window.

**Tech Stack:** C11, SDL2, inih (r62, default build flags), CMake + CTest. No new dependencies.

**Spec:** `docs/superpowers/specs/2026-10-16-config-foundation-design.md`. Read its "Plan-time amendments" section; it records where this plan deliberately deviates from the original spec text.

## Global Constraints

- **Language and style:** C11; 4-space indent; `snake_case` functions, `PascalCase` typedefs; `static` file-local functions with prototypes at the top of the file. Code must compile with MSVC and GCC/Clang, so no GNU-only extensions in shared files.
- **Platform guards:** Linux/macOS code goes under `#ifdef PLATFORM_POSIX` (never `__unix__`). Mac-only code goes under `#ifdef __APPLE__`, Windows-only code under `#ifdef _WIN32`.
- **No new third-party dependencies.** Tests use plain C asserts and CTest.
- **Config key names** come from the `SETTING_*` macros (`config/config_settings.cmake` → generated `launcher_config.h`). Never hard-code a key string that has a macro.
- **inih rules to mirror:**
  - `INI_START_COMMENT_PREFIXES ";#"`.
  - `INI_ALLOW_INLINE_COMMENTS 1` with prefix `;`: only a `;` preceded by whitespace starts a comment.
  - `INI_ALLOW_MULTILINE 1`: an indented line after a key is another value for that key.
  - `INI_ALLOW_BOM 1`.
  - Key/value separator is the first `=` or `:`.
  - `INI_MAX_LINE 200`: inih reads at most 199 bytes per line including the newline.
- **Executable/paths keep the `flex-launcher` name.**
- **Tests build only with `-DBUILD_TESTING=ON`** (default OFF), so the release CI is unaffected.
- **Error handling:** `log_error` for recoverable problems, `log_fatal` only at startup.

## Review Focus

These are failure modes the spec implies but which are easy to miss. Each has a test or manual step in the owning task.

1. **A command containing ` ;`** (e.g. `sh -c "a ; b"`). inih silently truncates it at the inline comment, so `ini_doc_set` must refuse the value and the caller must report it. *(Task 3)*
2. **Reloading re-runs the parser.** The hotkey and gamepad-control lists and the menu builder keep static or global "current" pointers. Parsing twice must not touch freed memory or keep stale tails. *(Tasks 7 and 9)*
3. **Lines longer than 199 bytes** (long icon paths or commands). inih splits them into garbage, so saving must reject them with a clear message. *(Task 8)*
4. **Menu sections whose keys aren't `EntryN`, and duplicate `[Section]` headers.** `config_handler` treats every key in a menu section as an entry, and inih merges duplicate sections. List read and write must do the same. *(Task 4)*
5. **A gamepad already connected when `:reload` runs.** SDL only announces devices when the gamepad subsystem starts, so the reload must re-open connected controllers itself. *(Task 9, manual)*

---

## File Structure

| File | Status | Responsibility |
|---|---|---|
| `src/ini_doc.h`, `src/ini_doc.c` | new | Line-preserving INI document: parse, query, edit, serialize, atomic save. No SDL, no logging. |
| `src/platform/fileops.h` | new | `replace_file()` prototype (atomic rename that overwrites). |
| `src/platform/unix.c`, `src/platform/win32.c` | modify | Implement `replace_file()`. Win32 also gets `unregister_exit_hotkey()`. |
| `src/settings_schema.h`, `src/settings_schema.c` | new | The `SettingDef` table for all 93 settings, lookup, and validation. |
| `src/config_defaults.c` | new | `const Config default_config` (moved out of `launcher.c` so tests can link it). |
| `src/config_io.h`, `src/config_io.c` | new | `config_check()` (structural checks) and `config_save()` (validate dirty keys + check + atomic save). |
| `src/util.c`, `src/util.h` | modify | Export `mode_settings`. Add `reset_config_parser()`. `parse_config_file()` returns `int`. |
| `src/launcher.h` | modify | `Config.config_path`, `State.reload_pending`, `SCMD_RELOAD`, `extern const Config default_config`. |
| `src/launcher.c` | modify | `launcher_load/unload/reload`, `create_renderer`, `:reload`, main-loop reload. |
| `src/image.c` | modify | `load_font()` uses `log_error` instead of `log_fatal`. |
| `src/CMakeLists.txt` | modify | Add the new sources. |
| `CMakeLists.txt` | modify | `BUILD_TESTING` option and `tests/` subdirectory. |
| `tests/CMakeLists.txt`, `tests/test.h`, `tests/test_main.c`, `tests/test_stubs.c`, `tests/test_ini_doc.c`, `tests/test_schema.c`, `tests/test_config_io.c` | new | Unit tests (`reflex-tests`, CTest suites `ini_doc`, `schema`, `config_io`). |
| `config/launcher_config.h.in` | unchanged | — |
| `docs/configuration.md`, `CHANGELOG`, `CLAUDE.md`, `.gitignore` | modify | Document `:reload` and tests. Ignore `build-*/`. |

Build/test commands used throughout (from the repo root):

```bash
cmake -B build-tests -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DEXTRA_WARNINGS=ON
cmake --build build-tests --target reflex-tests
ctest --test-dir build-tests --output-on-failure
```

---

### Task 1: Test harness + `ini_doc` parse/serialize round trip

**Files:**
- Create: `tests/CMakeLists.txt`, `tests/test.h`, `tests/test_main.c`, `tests/test_stubs.c`, `tests/test_ini_doc.c`, `src/ini_doc.h`, `src/ini_doc.c`
- Modify: `CMakeLists.txt` (end of file), `.gitignore`

**Interfaces:**
- Produces:
  - `IniDoc *ini_doc_parse(const char *text)`
  - `char *ini_doc_serialize(const IniDoc *doc)` (malloc'd)
  - `void ini_doc_free(IniDoc *doc)`
  - test macros `CHECK(cond)`, `CHECK_STR(a, b)`
  - helpers `void test_write_file(const char *path, const char *text)`, `char *test_read_file(const char *path)`, `void test_tmp_path(char *buf, size_t len, const char *name)`

- [ ] **Step 1: Add the CMake test wiring**

Append to the end of the top-level `CMakeLists.txt`:

```cmake
# Unit tests: cmake -DBUILD_TESTING=ON, then ctest
option(BUILD_TESTING "Build the unit tests" OFF)
if (BUILD_TESTING)
  enable_testing()
  add_subdirectory(tests)
endif ()
```

Create `tests/CMakeLists.txt`:

```cmake
set(TEST_SOURCES
  test_main.c
  test_stubs.c
  test_ini_doc.c
  ${PROJECT_SOURCE_DIR}/src/ini_doc.c
  ${PROJECT_SOURCE_DIR}/src/util.c
  ${PROJECT_SOURCE_DIR}/src/debug.c
)
set(TEST_SUITES ini_doc)

add_executable(reflex-tests ${TEST_SOURCES})
target_include_directories(reflex-tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tmp)
target_compile_definitions(reflex-tests PRIVATE
  TEST_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}/tmp"
  SAMPLE_CONFIG="${PROJECT_BINARY_DIR}/config.ini"
)
target_link_libraries(reflex-tests platform)
if (UNIX)
  target_link_libraries(reflex-tests PkgConfig::SDL2 PkgConfig::SDL2_IMAGE PkgConfig::SDL2_TTF PkgConfig::INIH m)
else ()
  target_link_libraries(reflex-tests
    $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
    $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
    $<IF:$<TARGET_EXISTS:SDL2_image::SDL2_image>,SDL2_image::SDL2_image,SDL2_image::SDL2_image-static>
    $<IF:$<TARGET_EXISTS:SDL2_ttf::SDL2_ttf>,SDL2_ttf::SDL2_ttf,SDL2_ttf::SDL2_ttf-static>
    ${INIH} ${GETOPT} PowrProf
  )
  target_include_directories(reflex-tests PRIVATE ${INIH_INCLUDE_DIR} ${GETOPT_INCLUDE_DIR})
endif ()

foreach (suite ${TEST_SUITES})
  add_test(NAME ${suite} COMMAND reflex-tests ${suite})
endforeach ()
```

Append `build-*/` to `.gitignore`.

- [ ] **Step 2: Write the harness**

`tests/test.h`:

```c
#ifndef TEST_H
#define TEST_H
#include <stdio.h>
#include <string.h>

extern int test_failures;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        test_failures++; \
    } \
} while (0)

#define CHECK_STR(actual, expected) do { \
    const char *_a = (actual), *_e = (expected); \
    if ((_a == NULL) != (_e == NULL) || (_a != NULL && strcmp(_a, _e) != 0)) { \
        fprintf(stderr, "%s:%d: CHECK_STR failed:\n  got:      \"%s\"\n  expected: \"%s\"\n", \
            __FILE__, __LINE__, _a ? _a : "(null)", _e ? _e : "(null)"); \
        test_failures++; \
    } \
} while (0)

void test_tmp_path(char *buf, size_t len, const char *name);
void test_write_file(const char *path, const char *text);
char *test_read_file(const char *path);

#endif
```

`tests/test_main.c`:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"

int test_failures = 0;
extern FILE *log_file;

void suite_ini_doc(void);

static const struct {
    const char *name;
    void (*run)(void);
} suites[] = {
    {"ini_doc", suite_ini_doc},
};

void test_tmp_path(char *buf, size_t len, const char *name)
{
    snprintf(buf, len, "%s/%s", TEST_TMP_DIR, name);
}

void test_write_file(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    fwrite(text, 1, strlen(text), file);
    fclose(file);
}

char *test_read_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return NULL;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *buffer = malloc((size_t) size + 1);
    size_t read = fread(buffer, 1, (size_t) size, file);
    buffer[read] = '\0';
    fclose(file);
    return buffer;
}

int main(int argc, char *argv[])
{
    log_file = stderr;
    for (size_t i = 0; i < sizeof(suites) / sizeof(suites[0]); i++) {
        if (argc < 2 || strcmp(argv[1], suites[i].name) == 0)
            suites[i].run();
    }
    if (test_failures) {
        fprintf(stderr, "%d check(s) failed\n", test_failures);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
```

`tests/test_stubs.c` provides the launcher globals that `util.c`, `debug.c`, and the platform library reference. `util.c` and `debug.c` are linked from this first task, because the platform library (pulled in from Task 5 by `replace_file`) depends on them:

```c
// Stand-ins for globals defined in launcher.c, so library code can be unit tested
#include <stdio.h>
#include <stdlib.h>
#include <SDL.h>
#include <SDL_syswm.h>
#include "launcher.h"

Config config;
FILE *log_file = NULL;
Hotkey *hotkeys = NULL;
GamepadControl *gamepad_controls = NULL;
#ifdef _WIN32
SDL_SysWMinfo wm_info;
#endif

void quit(int status)
{
    exit(status);
}

void print_version(FILE *stream)
{
    fputs("reflex-tests\n", stream);
}
```

- [ ] **Step 3: Write the failing round-trip tests**

`tests/test_ini_doc.c`:

```c
#include <stdlib.h>
#include "test.h"
#include "ini_doc.h"

static void check_round_trip(const char *text)
{
    IniDoc *doc = ini_doc_parse(text);
    char *out = ini_doc_serialize(doc);
    CHECK_STR(out, text);
    free(out);
    ini_doc_free(doc);
}

static void test_round_trip(void)
{
    check_round_trip("");
    check_round_trip("[General]\nDefaultMenu=Main\nVSync=true\n");
    check_round_trip("[General]\r\nDefaultMenu=Main\r\n\r\n# comment\r\n");
    check_round_trip("\xEF\xBB\xBF[General]\nDefaultMenu=Main\n");
    check_round_trip("[General]\nDefaultMenu=Main");                 // no trailing newline
    check_round_trip("# lead\n\n[A]\n  key = value ; note\n#Off=1\n;semi\n\tcontinued\nnot a key line\n[B]\n");
}

void suite_ini_doc(void)
{
    test_round_trip();
}
```

- [ ] **Step 4: Run to verify it fails**

Run: `cmake -B build-tests -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DEXTRA_WARNINGS=ON && cmake --build build-tests --target reflex-tests`
Expected: a build error. `src/ini_doc.c` doesn't exist yet and `ini_doc.h` isn't found.

- [ ] **Step 5: Implement parse/serialize**

`src/ini_doc.h`:

```c
#ifndef INI_DOC_H
#define INI_DOC_H
#include <stdbool.h>
#include <stddef.h>

// A line-preserving INI document. It follows inih's parsing rules so that
// ini_doc_get() returns what the launcher would load, and it writes back
// every comment, blank line, and unknown key unchanged.
typedef struct ini_doc IniDoc;

IniDoc *ini_doc_parse(const char *text);
IniDoc *ini_doc_load(const char *path);
char *ini_doc_serialize(const IniDoc *doc);
int ini_doc_save(const IniDoc *doc, const char *path);
void ini_doc_free(IniDoc *doc);

const char *ini_doc_get(const IniDoc *doc, const char *section, const char *key);
bool ini_doc_set(IniDoc *doc, const char *section, const char *key, const char *value);
void ini_doc_unset(IniDoc *doc, const char *section, const char *key);
bool ini_doc_value_is_representable(const char *value);

size_t ini_doc_get_list(const IniDoc *doc, const char *section, const char ***values);
bool ini_doc_set_list(IniDoc *doc, const char *section, const char *prefix,
                      const char *const *values, size_t count);

size_t ini_doc_sections(const IniDoc *doc, const char ***names);
bool ini_doc_has_section(const IniDoc *doc, const char *section);
void ini_doc_add_section(IniDoc *doc, const char *section);
void ini_doc_remove_section(IniDoc *doc, const char *section);
void ini_doc_rename_section(IniDoc *doc, const char *from, const char *to);

size_t ini_doc_dirty_count(const IniDoc *doc);
void ini_doc_dirty_get(const IniDoc *doc, size_t index, const char **section, const char **key);
bool ini_doc_changed_on_disk(const IniDoc *doc, const char *path);
void ini_doc_mark_saved(IniDoc *doc, const char *path);

#endif
```

`src/ini_doc.c`: only parse, serialize, and free in this task. The rest is added by later tasks.

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include "ini_doc.h"

typedef enum {
    LINE_BLANK,
    LINE_COMMENT,
    LINE_SECTION,
    LINE_KEY,
    LINE_COMMENTED_KEY,
    LINE_CONTINUATION,
    LINE_OTHER
} LineKind;

typedef struct {
    LineKind kind;
    char *text;     // the line as written, without its newline
    char *section;  // owning section ("" before the first header); a header's own name
    char *key;      // LINE_KEY, LINE_COMMENTED_KEY, LINE_CONTINUATION (the key it continues)
    char *value;    // the value inih reports for this line
} IniLine;

struct ini_doc {
    IniLine *lines;
    size_t count;
    size_t capacity;
    bool crlf;
    bool bom;
    bool trailing_newline;
    bool has_stat;
    long long file_size;
    long long file_mtime;
    char **dirty_sections;
    char **dirty_keys;
    size_t dirty_count;
};

static char *dup_range(const char *start, size_t length);
static char *trimmed(const char *start, const char *end);
static const char *find_chars_or_comment(const char *s, const char *chars);
static bool parse_commented_key(const char *s, char **key, char **value);
static IniLine classify_line(const char *raw, const char *section, const char *prev_key);
static void insert_line(IniDoc *doc, size_t index, IniLine line);
static void free_line(IniLine *line);

static char *dup_range(const char *start, size_t length)
{
    char *s = malloc(length + 1);
    memcpy(s, start, length);
    s[length] = '\0';
    return s;
}

static char *trimmed(const char *start, const char *end)
{
    while (start < end && isspace((unsigned char) *start))
        start++;
    while (end > start && isspace((unsigned char) end[-1]))
        end--;
    return dup_range(start, (size_t) (end - start));
}

// Mirrors inih's find_chars_or_comment(): stops at any char in `chars`,
// or at a ';' that follows whitespace (an inline comment)
static const char *find_chars_or_comment(const char *s, const char *chars)
{
    bool was_space = false;
    while (*s && (chars == NULL || strchr(chars, *s) == NULL) && !(was_space && *s == ';')) {
        was_space = isspace((unsigned char) *s);
        s++;
    }
    return s;
}

// A comment whose text is "key=value" with a space-free key, e.g. "#FPSLimit=60"
static bool parse_commented_key(const char *s, char **key, char **value)
{
    while (*s == ' ' || *s == '\t')
        s++;
    const char *key_start = s;
    while (*s && *s != '=' && !isspace((unsigned char) *s))
        s++;
    if (s == key_start)
        return false;
    const char *key_end = s;
    while (*s == ' ' || *s == '\t')
        s++;
    if (*s != '=')
        return false;
    *key = dup_range(key_start, (size_t) (key_end - key_start));
    *value = trimmed(s + 1, find_chars_or_comment(s + 1, NULL));
    return true;
}

// Classify one physical line (without its newline) with inih's rules
static IniLine classify_line(const char *raw, const char *section, const char *prev_key)
{
    IniLine line = { .kind = LINE_OTHER, .text = strdup(raw), .section = strdup(section) };
    const char *start = raw;
    while (*start && isspace((unsigned char) *start))
        start++;
    const char *end = raw + strlen(raw);
    while (end > start && isspace((unsigned char) end[-1]))
        end--;
    char *body = dup_range(start, (size_t) (end - start));

    if (*body == '\0')
        line.kind = LINE_BLANK;
    else if (*body == ';' || *body == '#')
        line.kind = parse_commented_key(body + 1, &line.key, &line.value) ? LINE_COMMENTED_KEY : LINE_COMMENT;
    else if (prev_key != NULL && start > raw) {
        line.kind = LINE_CONTINUATION;
        line.key = strdup(prev_key);
        line.value = trimmed(body, find_chars_or_comment(body, NULL));
    }
    else if (*body == '[') {
        const char *close = find_chars_or_comment(body + 1, "]");
        if (*close == ']') {
            line.kind = LINE_SECTION;
            free(line.section);
            line.section = dup_range(body + 1, (size_t) (close - body - 1));
        }
    }
    else {
        const char *separator = find_chars_or_comment(body, "=:");
        if (*separator == '=' || *separator == ':') {
            line.kind = LINE_KEY;
            line.key = trimmed(body, separator);
            line.value = trimmed(separator + 1, find_chars_or_comment(separator + 1, NULL));
        }
    }
    free(body);
    return line;
}

static void insert_line(IniDoc *doc, size_t index, IniLine line)
{
    if (doc->count == doc->capacity) {
        doc->capacity = doc->capacity ? doc->capacity * 2 : 64;
        doc->lines = realloc(doc->lines, doc->capacity * sizeof(IniLine));
    }
    memmove(doc->lines + index + 1, doc->lines + index, (doc->count - index) * sizeof(IniLine));
    doc->lines[index] = line;
    doc->count++;
}

static void free_line(IniLine *line)
{
    free(line->text);
    free(line->section);
    free(line->key);
    free(line->value);
}

IniDoc *ini_doc_parse(const char *text)
{
    IniDoc *doc = calloc(1, sizeof(IniDoc));
    if (strncmp(text, "\xEF\xBB\xBF", 3) == 0) {
        doc->bom = true;
        text += 3;
    }
    const char *first_newline = strchr(text, '\n');
    doc->crlf = first_newline != NULL && first_newline > text && first_newline[-1] == '\r';
    size_t length = strlen(text);
    doc->trailing_newline = length == 0 || text[length - 1] == '\n';

    char *section = strdup("");
    char *prev_key = NULL;
    const char *p = text;
    while (*p) {
        const char *newline = strchr(p, '\n');
        const char *line_end = newline ? newline : p + strlen(p);
        size_t n = (size_t) (line_end - p);
        if (n > 0 && p[n - 1] == '\r')
            n--;
        char *raw = dup_range(p, n);
        IniLine line = classify_line(raw, section, prev_key);
        free(raw);
        if (line.kind == LINE_SECTION) {
            free(section);
            section = strdup(line.section);
            free(prev_key);
            prev_key = NULL;
        }
        else if (line.kind == LINE_KEY) {
            free(prev_key);
            prev_key = strdup(line.key);
        }
        insert_line(doc, doc->count, line);
        p = newline ? newline + 1 : line_end;
    }
    free(section);
    free(prev_key);
    return doc;
}

char *ini_doc_serialize(const IniDoc *doc)
{
    const char *newline = doc->crlf ? "\r\n" : "\n";
    size_t newline_length = strlen(newline);
    size_t size = 4;
    for (size_t i = 0; i < doc->count; i++)
        size += strlen(doc->lines[i].text) + newline_length;
    char *out = malloc(size);
    char *p = out;
    if (doc->bom) {
        memcpy(p, "\xEF\xBB\xBF", 3);
        p += 3;
    }
    for (size_t i = 0; i < doc->count; i++) {
        size_t n = strlen(doc->lines[i].text);
        memcpy(p, doc->lines[i].text, n);
        p += n;
        if (i + 1 < doc->count || doc->trailing_newline) {
            memcpy(p, newline, newline_length);
            p += newline_length;
        }
    }
    *p = '\0';
    return out;
}

void ini_doc_free(IniDoc *doc)
{
    if (doc == NULL)
        return;
    for (size_t i = 0; i < doc->count; i++)
        free_line(&doc->lines[i]);
    free(doc->lines);
    for (size_t i = 0; i < doc->dirty_count; i++) {
        free(doc->dirty_sections[i]);
        free(doc->dirty_keys[i]);
    }
    free(doc->dirty_sections);
    free(doc->dirty_keys);
    free(doc);
}
```

- [ ] **Step 6: Run the tests**

Run: `cmake --build build-tests --target reflex-tests && ctest --test-dir build-tests --output-on-failure -R ini_doc`
Expected: PASS (`all checks passed`). Functions that are declared in `ini_doc.h` but not defined yet are fine, because nothing calls them.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt .gitignore tests/ src/ini_doc.h src/ini_doc.c
git commit -m "Add unit test harness and ini_doc parse/serialize"
```

---

### Task 2: `ini_doc_get` matches inih exactly

**Files:**
- Modify: `src/ini_doc.c`, `tests/test_ini_doc.c`

**Interfaces:**
- Consumes: Task 1's `ini_doc_parse`.
- Produces: `const char *ini_doc_get(const IniDoc *doc, const char *section, const char *key)`. It returns a pointer owned by `doc` that stays valid until the next edit of `doc`.

- [ ] **Step 1: Write the failing tests**

Add to `tests/test_ini_doc.c` (above `suite_ini_doc`) and call both from `suite_ini_doc()`:

```c
#include <ini.h>

typedef struct {
    const char *section;
    const char *key;
    char value[256];
    int found;
} LastValue;

// inih handler that remembers the last value reported for one section/key
static int last_value_handler(void *user, const char *section, const char *name, const char *value)
{
    LastValue *lv = user;
    if (strcmp(section, lv->section) == 0 && strcmp(name, lv->key) == 0) {
        snprintf(lv->value, sizeof(lv->value), "%s", value);
        lv->found = 1;
    }
    return 1;
}

static void check_parity(const char *text, const char *section, const char *key)
{
    LastValue lv = { section, key, "", 0 };
    ini_parse_string(text, last_value_handler, &lv);
    IniDoc *doc = ini_doc_parse(text);
    CHECK_STR(ini_doc_get(doc, section, key), lv.found ? lv.value : NULL);
    ini_doc_free(doc);
}

static void test_get_parity_with_inih(void)
{
    const char *corpus =
        "[A]\n"
        "plain=value\n"
        "spaced  =  padded value  \n"
        "inline=kept ; dropped\n"
        "nospace=a;b;c\n"
        "colon: yes\n"
        "#commented=ignored\n"
        "  ;indented comment\n"
        "multi=first\n"
        "  second\n"
        "dup=one\n"
        "dup=two\n"
        "[B]\n"
        "plain=b-value\n"
        "[A]\n"
        "late=merged\n";
    const char *keys[] = {"plain", "spaced", "inline", "nospace", "colon",
                          "commented", "multi", "dup", "late", "missing"};
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
        check_parity(corpus, "A", keys[i]);
    check_parity(corpus, "B", "plain");
}

static void test_get_basics(void)
{
    IniDoc *doc = ini_doc_parse("[General]\nVSync=false\n#FPSLimit=60\n");
    CHECK_STR(ini_doc_get(doc, "General", "VSync"), "false");
    CHECK_STR(ini_doc_get(doc, "General", "FPSLimit"), NULL);     // commented out
    CHECK_STR(ini_doc_get(doc, "general", "VSync"), NULL);        // case-sensitive
    CHECK_STR(ini_doc_get(doc, "Clock", "VSync"), NULL);
    ini_doc_free(doc);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build-tests --target reflex-tests`
Expected: link error, undefined `ini_doc_get`.

- [ ] **Step 3: Implement**

Add to `src/ini_doc.c` (with a `static bool line_matches(...)` prototype at the top):

```c
static bool line_matches(const IniLine *line, const char *section, const char *key)
{
    return line->key != NULL && strcmp(line->section, section) == 0 && strcmp(line->key, key) == 0;
}

// Last active value wins, like config_handler(); continuation lines count as values
const char *ini_doc_get(const IniDoc *doc, const char *section, const char *key)
{
    const char *value = NULL;
    for (size_t i = 0; i < doc->count; i++) {
        const IniLine *line = &doc->lines[i];
        if ((line->kind == LINE_KEY || line->kind == LINE_CONTINUATION) && line_matches(line, section, key))
            value = line->value;
    }
    return value;
}
```

- [ ] **Step 4: Run the tests**

Run: `cmake --build build-tests --target reflex-tests && ctest --test-dir build-tests --output-on-failure -R ini_doc`
Expected: PASS. If a parity check fails, fix `classify_line` to match inih. inih is the reference, so don't change the test.

- [ ] **Step 5: Commit**

```bash
git add src/ini_doc.c tests/test_ini_doc.c
git commit -m "ini_doc: get values with inih parity"
```

---

### Task 3: `ini_doc_set`, `ini_doc_unset`, dirty tracking

**Files:**
- Modify: `src/ini_doc.c`, `tests/test_ini_doc.c`

**Interfaces:**
- Consumes: Task 2's `line_matches`, `classify_line`.
- Produces:
  - `bool ini_doc_set(IniDoc*, const char *section, const char *key, const char *value)`: returns false and leaves the doc unchanged if `value` can't be stored losslessly.
  - `void ini_doc_unset(...)`
  - `bool ini_doc_value_is_representable(const char *value)`
  - `size_t ini_doc_dirty_count(const IniDoc*)`
  - `void ini_doc_dirty_get(const IniDoc*, size_t i, const char **section, const char **key)`
  - `void ini_doc_add_section(IniDoc*, const char*)`
  - `bool ini_doc_has_section(const IniDoc*, const char*)`

- [ ] **Step 1: Write the failing tests**

```c
static void check_set(const char *before, const char *section, const char *key,
                      const char *value, const char *after)
{
    IniDoc *doc = ini_doc_parse(before);
    CHECK(ini_doc_set(doc, section, key, value));
    char *out = ini_doc_serialize(doc);
    CHECK_STR(out, after);
    CHECK_STR(ini_doc_get(doc, section, key), value);
    free(out);
    ini_doc_free(doc);
}

static void test_set(void)
{
    // 1. replace in place, keeping neighbours and comments
    check_set("[General]\n# c\nVSync=true\nX=1\n", "General", "VSync", "false",
              "[General]\n# c\nVSync=false\nX=1\n");
    // replacing a multi-line value drops its continuation lines
    check_set("[A]\nk=one\n  two\nz=1\n", "A", "k", "new", "[A]\nk=new\nz=1\n");
    // 2. un-comment a commented-out key
    check_set("[General]\n#FPSLimit=\nVSync=true\n", "General", "FPSLimit", "60",
              "[General]\nFPSLimit=60\nVSync=true\n");
    // 3. append after the section's last setting, not after trailing comments
    check_set("[Clock]\nEnabled=true\n\n# Menus\n[Main]\nEntry1=a;b;c\n", "Clock", "ShowDate", "true",
              "[Clock]\nEnabled=true\nShowDate=true\n\n# Menus\n[Main]\nEntry1=a;b;c\n");
    // 3b. create a missing section at the end
    check_set("[General]\nVSync=true\n", "Clock", "Enabled", "true",
              "[General]\nVSync=true\n\n[Clock]\nEnabled=true\n");
    // CRLF files stay CRLF
    check_set("[General]\r\nVSync=true\r\n", "General", "VSync", "false", "[General]\r\nVSync=false\r\n");
}

static void test_set_rejects_lossy_values(void)
{
    IniDoc *doc = ini_doc_parse("[General]\nStartupCmd=old\n");
    CHECK(!ini_doc_set(doc, "General", "StartupCmd", "sh -c \"a ; b\""));  // inline comment
    CHECK(!ini_doc_set(doc, "General", "StartupCmd", "line1\nline2"));
    CHECK(!ini_doc_set(doc, "General", "StartupCmd", " padded"));
    CHECK(ini_doc_set(doc, "General", "StartupCmd", "a;b"));              // ';' without a space is fine
    CHECK(ini_doc_value_is_representable(""));
    CHECK_STR(ini_doc_get(doc, "General", "StartupCmd"), "a;b");
    ini_doc_free(doc);
}

static void test_unset_and_dirty(void)
{
    IniDoc *doc = ini_doc_parse("[General]\nVSync=false\n[Clock]\nEnabled=true\n");
    CHECK(ini_doc_dirty_count(doc) == 0);
    ini_doc_unset(doc, "General", "VSync");
    CHECK_STR(ini_doc_get(doc, "General", "VSync"), NULL);
    char *out = ini_doc_serialize(doc);
    CHECK_STR(out, "[General]\n#VSync=false\n[Clock]\nEnabled=true\n");
    free(out);
    ini_doc_set(doc, "Clock", "Enabled", "false");
    ini_doc_set(doc, "Clock", "Enabled", "true");   // same key twice -> one dirty entry
    CHECK(ini_doc_dirty_count(doc) == 2);
    const char *section, *key;
    ini_doc_dirty_get(doc, 1, &section, &key);
    CHECK_STR(section, "Clock");
    CHECK_STR(key, "Enabled");
    ini_doc_free(doc);
}
```

Call all three from `suite_ini_doc()`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build-tests --target reflex-tests`
Expected: link errors for `ini_doc_set`, `ini_doc_unset`, `ini_doc_dirty_count`, `ini_doc_dirty_get`, `ini_doc_value_is_representable`.

- [ ] **Step 3: Implement**

Add these prototypes to the top of `src/ini_doc.c` and the definitions below:

```c
static void delete_line(IniDoc *doc, size_t index);
static void make_key_line(IniLine *line, const char *key, const char *value);
static size_t section_insert_point(IniDoc *doc, const char *section);
static void mark_dirty(IniDoc *doc, const char *section, const char *key);

static void delete_line(IniDoc *doc, size_t index)
{
    free_line(&doc->lines[index]);
    memmove(doc->lines + index, doc->lines + index + 1, (doc->count - index - 1) * sizeof(IniLine));
    doc->count--;
}

// Rewrite a line as an active, unindented "key=value"
// (an indented key line would be read by inih as a continuation)
static void make_key_line(IniLine *line, const char *key, const char *value)
{
    size_t n = strlen(key) + strlen(value) + 2;
    char *text = malloc(n);
    snprintf(text, n, "%s=%s", key, value);
    free(line->text);
    line->text = text;
    char *new_key = strdup(key);
    free(line->key);
    line->key = new_key;
    free(line->value);
    line->value = strdup(value);
    line->kind = LINE_KEY;
}

// Index just after the last setting or header of `section`; creates the section if missing
static size_t section_insert_point(IniDoc *doc, const char *section)
{
    size_t last = (size_t) -1;
    for (size_t i = 0; i < doc->count; i++) {
        const IniLine *line = &doc->lines[i];
        if (strcmp(line->section, section) == 0 &&
        (line->kind == LINE_SECTION || line->kind == LINE_KEY ||
        line->kind == LINE_COMMENTED_KEY || line->kind == LINE_CONTINUATION))
            last = i;
    }
    if (last != (size_t) -1)
        return last + 1;
    ini_doc_add_section(doc, section);
    return doc->count;
}

static void mark_dirty(IniDoc *doc, const char *section, const char *key)
{
    for (size_t i = 0; i < doc->dirty_count; i++) {
        if (strcmp(doc->dirty_sections[i], section) == 0 && strcmp(doc->dirty_keys[i], key) == 0)
            return;
    }
    doc->dirty_sections = realloc(doc->dirty_sections, (doc->dirty_count + 1) * sizeof(char*));
    doc->dirty_keys = realloc(doc->dirty_keys, (doc->dirty_count + 1) * sizeof(char*));
    doc->dirty_sections[doc->dirty_count] = strdup(section);
    doc->dirty_keys[doc->dirty_count] = strdup(key);
    doc->dirty_count++;
}

// True if inih would read `value` back unchanged from "key=value"
bool ini_doc_value_is_representable(const char *value)
{
    if (strchr(value, '\n') != NULL || strchr(value, '\r') != NULL)
        return false;
    size_t n = strlen(value);
    if (n > 0 && (isspace((unsigned char) value[0]) || isspace((unsigned char) value[n - 1])))
        return false;
    return *find_chars_or_comment(value, NULL) == '\0';
}

bool ini_doc_set(IniDoc *doc, const char *section, const char *key, const char *value)
{
    if (!ini_doc_value_is_representable(value))
        return false;

    // Replace the last active instance (and drop its continuation lines)
    for (size_t i = doc->count; i-- > 0;) {
        IniLine *line = &doc->lines[i];
        if (line->kind == LINE_KEY && line_matches(line, section, key)) {
            make_key_line(line, key, value);
            while (i + 1 < doc->count && doc->lines[i + 1].kind == LINE_CONTINUATION)
                delete_line(doc, i + 1);
            mark_dirty(doc, section, key);
            return true;
        }
    }

    // Un-comment a commented-out instance
    for (size_t i = doc->count; i-- > 0;) {
        IniLine *line = &doc->lines[i];
        if (line->kind == LINE_COMMENTED_KEY && line_matches(line, section, key)) {
            make_key_line(line, key, value);
            mark_dirty(doc, section, key);
            return true;
        }
    }

    // Append to the section
    size_t at = section_insert_point(doc, section);
    IniLine line = { .kind = LINE_KEY, .text = strdup(""), .section = strdup(section) };
    insert_line(doc, at, line);
    make_key_line(&doc->lines[at], key, value);
    mark_dirty(doc, section, key);
    return true;
}

void ini_doc_unset(IniDoc *doc, const char *section, const char *key)
{
    for (size_t i = 0; i < doc->count; i++) {
        IniLine *line = &doc->lines[i];
        if (line->kind == LINE_KEY && line_matches(line, section, key)) {
            size_t n = strlen(line->text) + 2;
            char *text = malloc(n);
            snprintf(text, n, "#%s", line->text);
            free(line->text);
            line->text = text;
            line->kind = LINE_COMMENTED_KEY;
            mark_dirty(doc, section, key);
        }
    }
}

size_t ini_doc_dirty_count(const IniDoc *doc)
{
    return doc->dirty_count;
}

void ini_doc_dirty_get(const IniDoc *doc, size_t index, const char **section, const char **key)
{
    *section = doc->dirty_sections[index];
    *key = doc->dirty_keys[index];
}

bool ini_doc_has_section(const IniDoc *doc, const char *section)
{
    for (size_t i = 0; i < doc->count; i++) {
        if (doc->lines[i].kind == LINE_SECTION && strcmp(doc->lines[i].section, section) == 0)
            return true;
    }
    return false;
}

void ini_doc_add_section(IniDoc *doc, const char *section)
{
    if (ini_doc_has_section(doc, section))
        return;
    if (doc->count > 0 && doc->lines[doc->count - 1].kind != LINE_BLANK) {
        IniLine blank = {
            .kind = LINE_BLANK,
            .text = strdup(""),
            .section = strdup(doc->lines[doc->count - 1].section)
        };
        insert_line(doc, doc->count, blank);
    }
    size_t n = strlen(section) + 3;
    char *text = malloc(n);
    snprintf(text, n, "[%s]", section);
    IniLine header = { .kind = LINE_SECTION, .text = text, .section = strdup(section) };
    insert_line(doc, doc->count, header);
    doc->trailing_newline = true;
}
```

Note: `unset` keeps the original line text, e.g. `#VSync=false`, so the user can still see the previous value. `make_key_line` writes `key=value` with no spaces, matching the shipped file's style.

- [ ] **Step 4: Run the tests**

Run: `cmake --build build-tests --target reflex-tests && ctest --test-dir build-tests --output-on-failure -R ini_doc`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/ini_doc.c tests/test_ini_doc.c
git commit -m "ini_doc: set/unset with comment-preserving edits and dirty tracking"
```

---

### Task 4: Lists and sections

**Files:**
- Modify: `src/ini_doc.c`, `tests/test_ini_doc.c`

**Interfaces:**
- Consumes: Task 3's `make_key_line`, `section_insert_point`, `mark_dirty`, `delete_line`.
- Produces:
  - `size_t ini_doc_get_list(const IniDoc*, const char *section, const char ***values)`: returns **every** active key value in the section, in file order, including duplicate section blocks and continuation lines. This mirrors `config_handler`, where every key in a menu or `[Hotkeys]` section is an item. The caller `free()`s the array; the strings are owned by the doc.
  - `bool ini_doc_set_list(IniDoc*, const char *section, const char *prefix, const char *const *values, size_t count)`: rewrites all list lines as `<prefix>1..<prefix>count`. Returns false, with the doc unchanged, if any value isn't representable. Marks `(section, prefix)` dirty.
  - `size_t ini_doc_sections(const IniDoc*, const char ***names)`: unique section names in order, excluding `""`. The caller `free()`s the array.
  - `void ini_doc_remove_section(IniDoc*, const char*)`
  - `void ini_doc_rename_section(IniDoc*, const char *from, const char *to)`

- [ ] **Step 1: Write the failing tests**

```c
static void check_list(IniDoc *doc, const char *section, const char *const *expected, size_t n)
{
    const char **values = NULL;
    size_t count = ini_doc_get_list(doc, section, &values);
    CHECK(count == n);
    for (size_t i = 0; i < n && i < count; i++)
        CHECK_STR(values[i], expected[i]);
    free(values);
}

static void test_get_list_mirrors_config_handler(void)
{
    // any key name counts, duplicate sections merge, continuation lines are extra items
    IniDoc *doc = ini_doc_parse(
        "[Main]\nEntry1=A;a.png;cmd-a\nKodi=B;b.png;cmd-b\n#Entry9=off;x;y\n"
        "[Other]\nEntry1=X;x;x\n"
        "[Main]\nEntry3=C;c.png;cmd-c\n  D;d.png;cmd-d\n");
    const char *expected[] = {"A;a.png;cmd-a", "B;b.png;cmd-b", "C;c.png;cmd-c", "D;d.png;cmd-d"};
    check_list(doc, "Main", expected, 4);
    ini_doc_free(doc);
}

static void check_set_list(const char *before, const char *const *values, size_t n, const char *after)
{
    IniDoc *doc = ini_doc_parse(before);
    CHECK(ini_doc_set_list(doc, "Main", "Entry", values, n));
    char *out = ini_doc_serialize(doc);
    CHECK_STR(out, after);
    free(out);
    check_list(doc, "Main", values, n);
    ini_doc_free(doc);
}

static void test_set_list(void)
{
    const char *base = "[Main]\nEntry1=A;a;a\n# keep me\nEntry2=B;b;b\n\n[System]\nEntry1=S;s;s\n";
    const char *reordered[] = {"B;b;b", "A;a;a"};
    check_set_list(base, reordered, 2,
        "[Main]\nEntry1=B;b;b\n# keep me\nEntry2=A;a;a\n\n[System]\nEntry1=S;s;s\n");
    const char *grown[] = {"A;a;a", "B;b;b", "C;c;c"};
    check_set_list(base, grown, 3,
        "[Main]\nEntry1=A;a;a\n# keep me\nEntry2=B;b;b\nEntry3=C;c;c\n\n[System]\nEntry1=S;s;s\n");
    const char *shrunk[] = {"A;a;a"};
    check_set_list(base, shrunk, 1,
        "[Main]\nEntry1=A;a;a\n# keep me\n\n[System]\nEntry1=S;s;s\n");
    const char *fresh[] = {"N;n;n"};
    check_set_list("[General]\nDefaultMenu=Main\n", fresh, 1,
        "[General]\nDefaultMenu=Main\n\n[Main]\nEntry1=N;n;n\n");

    IniDoc *doc = ini_doc_parse(base);
    const char *bad[] = {"A;a;a", "B;b;cmd ; oops"};
    CHECK(!ini_doc_set_list(doc, "Main", "Entry", bad, 2));
    char *out = ini_doc_serialize(doc);
    CHECK_STR(out, base);   // unchanged on failure
    free(out);
    ini_doc_free(doc);
}

static void test_sections(void)
{
    IniDoc *doc = ini_doc_parse("top=1\n[General]\nX=1\n[Main]\nEntry1=a;b;c\n[System]\nEntry1=s;s;s\n[Main]\nEntry2=d;e;f\n");
    const char **names = NULL;
    size_t n = ini_doc_sections(doc, &names);
    CHECK(n == 3);
    if (n == 3) {
        CHECK_STR(names[0], "General");
        CHECK_STR(names[1], "Main");
        CHECK_STR(names[2], "System");
    }
    free(names);

    ini_doc_rename_section(doc, "Main", "Home");
    CHECK(!ini_doc_has_section(doc, "Main"));
    const char *home[] = {"a;b;c", "d;e;f"};
    check_list(doc, "Home", home, 2);

    ini_doc_remove_section(doc, "System");
    CHECK(!ini_doc_has_section(doc, "System"));
    char *out = ini_doc_serialize(doc);
    CHECK_STR(out, "top=1\n[General]\nX=1\n[Home]\nEntry1=a;b;c\n[Home]\nEntry2=d;e;f\n");
    free(out);
    ini_doc_free(doc);
}
```

Call all four from `suite_ini_doc()`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build-tests --target reflex-tests`
Expected: link errors for the list and section functions.

- [ ] **Step 3: Implement**

```c
static bool is_list_line(const IniLine *line, const char *section)
{
    return (line->kind == LINE_KEY || line->kind == LINE_CONTINUATION) && strcmp(line->section, section) == 0;
}

size_t ini_doc_get_list(const IniDoc *doc, const char *section, const char ***values)
{
    size_t count = 0;
    *values = malloc((doc->count + 1) * sizeof(char*));
    for (size_t i = 0; i < doc->count; i++) {
        if (is_list_line(&doc->lines[i], section))
            (*values)[count++] = doc->lines[i].value;
    }
    return count;
}

bool ini_doc_set_list(IniDoc *doc, const char *section, const char *prefix,
                      const char *const *values, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (!ini_doc_value_is_representable(values[i]))
            return false;
    }

    size_t *existing = malloc((doc->count + 1) * sizeof(size_t));
    size_t existing_count = 0;
    for (size_t i = 0; i < doc->count; i++) {
        if (is_list_line(&doc->lines[i], section))
            existing[existing_count++] = i;
    }

    char key[64];
    size_t reused = count < existing_count ? count : existing_count;
    for (size_t i = 0; i < reused; i++) {
        snprintf(key, sizeof(key), "%s%zu", prefix, i + 1);
        make_key_line(&doc->lines[existing[i]], key, values[i]);
    }

    // Delete surplus lines from the back so earlier indices stay valid
    for (size_t j = existing_count; j-- > count;)
        delete_line(doc, existing[j]);

    // Insert new lines after the last reused one, or at the section's insert point
    size_t at = reused > 0 ? existing[reused - 1] + 1 : section_insert_point(doc, section);
    for (size_t i = reused; i < count; i++) {
        snprintf(key, sizeof(key), "%s%zu", prefix, i + 1);
        IniLine line = { .kind = LINE_KEY, .text = strdup(""), .section = strdup(section) };
        insert_line(doc, at, line);
        make_key_line(&doc->lines[at], key, values[i]);
        at++;
    }
    free(existing);
    mark_dirty(doc, section, prefix);
    return true;
}

size_t ini_doc_sections(const IniDoc *doc, const char ***names)
{
    size_t count = 0;
    *names = malloc((doc->count + 1) * sizeof(char*));
    for (size_t i = 0; i < doc->count; i++) {
        if (doc->lines[i].kind != LINE_SECTION)
            continue;
        bool seen = false;
        for (size_t j = 0; j < count; j++)
            seen = seen || strcmp((*names)[j], doc->lines[i].section) == 0;
        if (!seen)
            (*names)[count++] = doc->lines[i].section;
    }
    return count;
}

void ini_doc_remove_section(IniDoc *doc, const char *section)
{
    for (size_t i = doc->count; i-- > 0;) {
        if (strcmp(doc->lines[i].section, section) == 0)
            delete_line(doc, i);
    }
}

void ini_doc_rename_section(IniDoc *doc, const char *from, const char *to)
{
    char *from_copy = strdup(from);  // `from` may point into the doc
    for (size_t i = 0; i < doc->count; i++) {
        IniLine *line = &doc->lines[i];
        if (strcmp(line->section, from_copy) != 0)
            continue;
        free(line->section);
        line->section = strdup(to);
        if (line->kind == LINE_SECTION) {
            size_t n = strlen(to) + 3;
            free(line->text);
            line->text = malloc(n);
            snprintf(line->text, n, "[%s]", to);
        }
    }
    free(from_copy);
}
```

Note: `remove_section` deletes every line attributed to the section, including blank and comment lines up to the next header. That's acceptable, and the `test_sections` expectation pins it.

- [ ] **Step 4: Run the tests**

Run: `cmake --build build-tests --target reflex-tests && ctest --test-dir build-tests --output-on-failure -R ini_doc`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/ini_doc.c tests/test_ini_doc.c
git commit -m "ini_doc: numbered lists and section operations"
```

---

### Task 5: Load, atomic save with backup, change detection

**Files:**
- Create: `src/platform/fileops.h`
- Modify: `src/ini_doc.c`, `src/platform/unix.c`, `src/platform/win32.c`, `src/platform/CMakeLists.txt`, `tests/test_ini_doc.c`

**Interfaces:**
- Produces:
  - `int replace_file(const char *from, const char *to)`: 0 on success. Overwrites `to`.
  - `IniDoc *ini_doc_load(const char *path)`: NULL if unreadable. Records size and mtime.
  - `int ini_doc_save(const IniDoc*, const char *path)`: 0 on success; writes `path.tmp`, copies the old file to `path.bak`, then renames.
  - `bool ini_doc_changed_on_disk(const IniDoc*, const char *path)`: false for docs that weren't loaded from disk.
  - `void ini_doc_mark_saved(IniDoc*, const char *path)`: refreshes the size/mtime and clears the dirty keys.

- [ ] **Step 1: Write the failing tests**

```c
static void test_load_save(void)
{
    char dir[512], path[512], bak[512], tmp[512];
    test_tmp_path(dir, sizeof(dir), "dir with space");
#ifdef _WIN32
    _mkdir(dir);
#else
    mkdir(dir, 0700);
#endif
    snprintf(path, sizeof(path), "%s/config.ini", dir);
    snprintf(bak, sizeof(bak), "%s.bak", path);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    remove(bak);
    test_write_file(path, "[General]\r\nVSync=true\r\n");

    IniDoc *doc = ini_doc_load(path);
    CHECK(doc != NULL);
    CHECK(!ini_doc_changed_on_disk(doc, path));
    ini_doc_set(doc, "General", "VSync", "false");
    CHECK(ini_doc_save(doc, path) == 0);
    ini_doc_mark_saved(doc, path);
    CHECK(ini_doc_dirty_count(doc) == 0);

    char *saved = test_read_file(path);
    char *backup = test_read_file(bak);
    CHECK_STR(saved, "[General]\r\nVSync=false\r\n");
    CHECK_STR(backup, "[General]\r\nVSync=true\r\n");
    CHECK(test_read_file(tmp) == NULL);
    free(saved);
    free(backup);

    // another program edits the file -> detected (size changes)
    test_write_file(path, "[General]\nVSync=true\nWrapEntries=true\n");
    CHECK(ini_doc_changed_on_disk(doc, path));

    // a doc parsed from memory never reports a disk change
    IniDoc *memory = ini_doc_parse("[A]\n");
    CHECK(!ini_doc_changed_on_disk(memory, path));
    ini_doc_free(memory);

    // saving into a missing directory fails cleanly and leaves no .tmp
    char missing[512];
    test_tmp_path(missing, sizeof(missing), "no/such/dir/config.ini");
    CHECK(ini_doc_save(doc, missing) != 0);
    ini_doc_free(doc);

    CHECK(ini_doc_load(missing) == NULL);
}
```

Add `#include <sys/stat.h>`, plus `#ifdef _WIN32 #include <direct.h> #endif`, at the top of `tests/test_ini_doc.c`, and call the test from `suite_ini_doc()`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build-tests --target reflex-tests`
Expected: link errors for `ini_doc_load`, `ini_doc_save`, `ini_doc_changed_on_disk`, `ini_doc_mark_saved`.

- [ ] **Step 3: Implement `replace_file`**

`src/platform/fileops.h`:

```c
#ifndef FILEOPS_H
#define FILEOPS_H

// Atomically move `from` onto `to`, replacing `to` if it exists. Returns 0 on success.
int replace_file(const char *from, const char *to);

#endif
```

In `src/platform/unix.c`, add `#include <stdio.h>` and `#include "fileops.h"` to the includes, then:

```c
// A function to atomically replace a file
int replace_file(const char *from, const char *to)
{
    return rename(from, to) == 0 ? 0 : -1;
}
```

In `src/platform/win32.c`, add `#include "fileops.h"`, then:

```c
// A function to atomically replace a file
// (ANSI API on purpose: paths come from fopen()-compatible strings elsewhere in the app)
int replace_file(const char *from, const char *to)
{
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? 0 : -1;
}
```

In `src/platform/CMakeLists.txt`, add `fileops.h` to both `add_library(platform ...)` source lists.

- [ ] **Step 4: Implement load/save in `src/ini_doc.c`**

Add `#include <sys/stat.h>` and `#include "platform/fileops.h"`, prototypes for the static helpers, and:

```c
static char *read_file_bytes(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return NULL;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size < 0) {
        fclose(file);
        return NULL;
    }
    char *buffer = malloc((size_t) size + 1);
    size_t read = fread(buffer, 1, (size_t) size, file);
    fclose(file);
    if (read != (size_t) size) {
        free(buffer);
        return NULL;
    }
    buffer[size] = '\0';
    return buffer;
}

static bool stat_file(const char *path, long long *size, long long *mtime)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return false;
    *size = (long long) st.st_size;
    *mtime = (long long) st.st_mtime;
    return true;
}

// Copies `from` to `to` if `from` exists. Returns 0 on success or when there is nothing to copy.
static int backup_file(const char *from, const char *to)
{
    struct stat st;
    if (stat(from, &st) != 0)
        return 0;
    char *bytes = read_file_bytes(from);
    if (bytes == NULL)
        return -1;
    size_t length = (size_t) st.st_size;
    FILE *file = fopen(to, "wb");
    if (file == NULL) {
        free(bytes);
        return -1;
    }
    bool ok = fwrite(bytes, 1, length, file) == length;
    ok = fclose(file) == 0 && ok;
    free(bytes);
    return ok ? 0 : -1;
}

IniDoc *ini_doc_load(const char *path)
{
    char *text = read_file_bytes(path);
    if (text == NULL)
        return NULL;
    IniDoc *doc = ini_doc_parse(text);
    free(text);
    doc->has_stat = stat_file(path, &doc->file_size, &doc->file_mtime);
    return doc;
}

int ini_doc_save(const IniDoc *doc, const char *path)
{
    char *text = ini_doc_serialize(doc);
    size_t n = strlen(path) + 5;
    char *tmp = malloc(n);
    char *bak = malloc(n);
    snprintf(tmp, n, "%s.tmp", path);
    snprintf(bak, n, "%s.bak", path);

    int status = -1;
    FILE *file = fopen(tmp, "wb");
    if (file != NULL) {
        size_t length = strlen(text);
        bool ok = fwrite(text, 1, length, file) == length;
        ok = fflush(file) == 0 && ok;
        ok = fclose(file) == 0 && ok;
        if (ok && backup_file(path, bak) == 0 && replace_file(tmp, path) == 0)
            status = 0;
        if (status != 0)
            remove(tmp);
    }
    free(text);
    free(tmp);
    free(bak);
    return status;
}

bool ini_doc_changed_on_disk(const IniDoc *doc, const char *path)
{
    if (!doc->has_stat)
        return false;
    long long size, mtime;
    if (!stat_file(path, &size, &mtime))
        return true;
    return size != doc->file_size || mtime != doc->file_mtime;
}

void ini_doc_mark_saved(IniDoc *doc, const char *path)
{
    doc->has_stat = stat_file(path, &doc->file_size, &doc->file_mtime);
    for (size_t i = 0; i < doc->dirty_count; i++) {
        free(doc->dirty_sections[i]);
        free(doc->dirty_keys[i]);
    }
    doc->dirty_count = 0;
}
```

- [ ] **Step 5: Run the tests**

Run: `cmake --build build-tests --target reflex-tests && ctest --test-dir build-tests --output-on-failure -R ini_doc`
Expected: PASS. Also confirm the main app still builds: `cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build`.

- [ ] **Step 6: Commit**

```bash
git add src/platform/ src/ini_doc.c tests/test_ini_doc.c
git commit -m "ini_doc: load, atomic save with .bak, change detection"
```

---

### Task 6: Settings schema

**Files:**
- Create: `src/settings_schema.h`, `src/settings_schema.c`, `tests/test_schema.c`
- Modify: `src/util.c:27` (export `mode_settings`), `src/util.h`, `src/CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/test_main.c`

**Interfaces:**
- Consumes:
  - `convert_bool`, `hex_to_color`, `is_percent`, and `mode_settings[][5]` (`util.h`)
  - `ModeSettingType` indices (`launcher.h`)
  - `SETTING_*` and `MIN_*`/`MAX_*` macros
  - Task 5's `ini_doc_load`/`ini_doc_get`
- Produces:
  - `typedef enum SettingType`: `SET_BOOL, SET_CHOICE, SET_INT, SET_PERCENT, SET_LENGTH, SET_DECIMAL, SET_COLOR, SET_TEXT, SET_FILE, SET_DIR, SET_FONT, SET_COMMAND, SET_MENU_REF`
  - `typedef struct SettingDef { const char *id, *section, *key, *label, *help; SettingType type; int min, max; const char *const *choices; const char *default_value; }`
  - `extern const SettingDef settings_schema[]`, `extern const size_t settings_schema_count`
  - `extern const char *const schema_sections[]` (NULL-terminated)
  - `const SettingDef *schema_find(const char *section, const char *key)`
  - `size_t schema_section_settings(const char *section, const SettingDef **out, size_t max)`
  - `bool schema_validate(const SettingDef *def, const char *value, char *err, size_t err_len)`

`default_value` is the value in the shipped sample `config.ini` (from `config_settings.cmake`), or NULL when the sample leaves it unset. The UI pre-fills it when a setting is switched on. "Reset to default" means `ini_doc_unset`, which makes the parser use its built-in default.

- [ ] **Step 1: Export `mode_settings`**

In `src/util.c:27`, change `static const char *mode_settings[][5] = {` to `const char *mode_settings[][5] = {`. In `src/util.h`, add `extern const char *mode_settings[][5];` above the prototypes.

- [ ] **Step 2: Write the failing tests**

`tests/test_schema.c`:

```c
#include <stdlib.h>
#include <SDL.h>
#include "test.h"
#include "launcher.h"
#include <launcher_config.h>
#include "settings_schema.h"
#include "ini_doc.h"

static void test_every_setting_macro_is_in_the_table_once(void)
{
    char *keys = test_read_file(SETTING_KEYS_FILE);   // "SETTING_X=Value" lines, generated by CMake
    CHECK(keys != NULL);
    if (keys == NULL)
        return;
    size_t macros = 0;
    for (char *line = strtok(keys, "\n"); line != NULL; line = strtok(NULL, "\n")) {
        char *eq = strchr(line, '=');
        if (eq == NULL)
            continue;
        *eq = '\0';
        macros++;
        size_t matches = 0;
        for (size_t i = 0; i < settings_schema_count; i++) {
            if (strcmp(settings_schema[i].id, line) == 0) {
                matches++;
                CHECK_STR(settings_schema[i].key, eq + 1);
            }
        }
        if (matches != 1)
            fprintf(stderr, "  %s appears %zu times in settings_schema\n", line, matches);
        CHECK(matches == 1);
    }
    CHECK(macros == settings_schema_count);
    free(keys);
}

static void test_section_key_pairs_are_unique(void)
{
    for (size_t i = 0; i < settings_schema_count; i++) {
        CHECK(schema_find(settings_schema[i].section, settings_schema[i].key) == &settings_schema[i]);
        CHECK(settings_schema[i].label != NULL && settings_schema[i].help != NULL);
    }
}

static void test_defaults_match_shipped_sample(void)
{
    IniDoc *sample = ini_doc_load(SAMPLE_CONFIG);
    CHECK(sample != NULL);
    if (sample == NULL)
        return;
    char *text = ini_doc_serialize(sample);
    for (size_t i = 0; i < settings_schema_count; i++) {
        const SettingDef *def = &settings_schema[i];
        if (def->type == SET_FONT)
            continue;   // sample uses build-dir font paths
        const char *value = ini_doc_get(sample, def->section, def->key);
        if (value == NULL) {
            // "#Key=value" lines in the sample still document the default
            char needle[128];
            snprintf(needle, sizeof(needle), "\n#%s=", def->key);
            const char *hit = strstr(text, needle);
            static char commented[128];
            commented[0] = '\0';
            if (hit != NULL)
                sscanf(hit + strlen(needle), "%127[^\r\n]", commented);
            value = commented[0] ? commented : NULL;
        }
        if ((value == NULL) != (def->default_value == NULL) ||
        (value != NULL && strcmp(value, def->default_value) != 0))
            fprintf(stderr, "  [%s] %s: sample \"%s\" vs schema \"%s\"\n", def->section, def->key,
                value ? value : "(unset)", def->default_value ? def->default_value : "(NULL)");
        CHECK_STR(def->default_value, value);
    }
    free(text);
    ini_doc_free(sample);
}

static void test_defaults_validate(void)
{
    char err[256];
    for (size_t i = 0; i < settings_schema_count; i++) {
        const SettingDef *def = &settings_schema[i];
        if (def->default_value != NULL && !schema_validate(def, def->default_value, err, sizeof(err)))
            fprintf(stderr, "  [%s] %s default rejected: %s\n", def->section, def->key, err);
        CHECK(def->default_value == NULL || schema_validate(def, def->default_value, err, sizeof(err)));
    }
}

static void check_valid(const char *section, const char *key, const char *value, bool expected)
{
    char err[256] = "";
    const SettingDef *def = schema_find(section, key);
    CHECK(def != NULL);
    if (def == NULL)
        return;
    bool valid = schema_validate(def, value, err, sizeof(err));
    if (valid != expected)
        fprintf(stderr, "  [%s] %s=\"%s\" expected %s\n", section, key, value, expected ? "valid" : "invalid");
    CHECK(valid == expected);
    if (!valid)
        CHECK(err[0] != '\0');
}

static void test_validation_boundaries(void)
{
    check_valid("General", SETTING_VSYNC, "true", true);
    check_valid("General", SETTING_VSYNC, "True", true);
    check_valid("General", SETTING_VSYNC, "yes", false);
    check_valid("General", SETTING_ON_LAUNCH, "Quit", true);
    check_valid("General", SETTING_ON_LAUNCH, "quit", false);
    check_valid("Layout", SETTING_ICON_SIZE, "32", true);
    check_valid("Layout", SETTING_ICON_SIZE, "1024", true);
    check_valid("Layout", SETTING_ICON_SIZE, "31", false);
    check_valid("Layout", SETTING_ICON_SIZE, "1025", false);
    check_valid("Layout", SETTING_ICON_SIZE, "64px", false);
    check_valid("Layout", SETTING_ICON_SPACING, "5%", true);
    check_valid("Layout", SETTING_ICON_SPACING, "40", true);
    check_valid("Layout", SETTING_ICON_SPACING, "-1", false);
    check_valid("Layout", SETTING_VCENTER, "25%", true);
    check_valid("Layout", SETTING_VCENTER, "80%", false);
    check_valid("Titles", SETTING_TITLE_OPACITY, "100%", true);
    check_valid("Titles", SETTING_TITLE_OPACITY, "101%", false);
    check_valid("Titles", SETTING_TITLE_OPACITY, "50", false);
    check_valid("Background", SETTING_BACKGROUND_COLOR, "#00FFaa", true);
    check_valid("Background", SETTING_BACKGROUND_COLOR, "#00FFa", false);
    check_valid("Background", SETTING_BACKGROUND_COLOR, "#00FFag", false);
    check_valid("Background", SETTING_BACKGROUND_COLOR, "00FFaa", false);
    check_valid("Background", SETTING_SLIDESHOW_TRANSITION_TIME, "1.5", true);
    check_valid("Background", SETTING_SLIDESHOW_TRANSITION_TIME, "3.5", false);
    check_valid("Background", SETTING_BACKGROUND_IMAGE, "", false);
    check_valid("General", SETTING_STARTUP_CMD, "", false);
    check_valid("Gamepad", SETTING_GAMEPAD_DEVICE, "-1", true);
    check_valid("Gamepad", SETTING_GAMEPAD_BUTTON_A, ":select", true);
    check_valid("Screensaver", SETTING_SCREENSAVER_IDLE_TIME, "2", false);
}

void suite_schema(void)
{
    test_every_setting_macro_is_in_the_table_once();
    test_section_key_pairs_are_unique();
    test_defaults_match_shipped_sample();
    test_defaults_validate();
    test_validation_boundaries();
}
```

In `tests/test_main.c`, declare `void suite_schema(void);` and add `{"schema", suite_schema},` to `suites[]`.

In `tests/CMakeLists.txt`:
- add `test_schema.c` and `${PROJECT_SOURCE_DIR}/src/settings_schema.c` to `TEST_SOURCES`
- set `TEST_SUITES` to `ini_doc schema`
- before `add_executable`, add:

```cmake
# Every SETTING_* key from config_settings.cmake, for the schema coverage test
get_cmake_property(ALL_VARIABLES VARIABLES)
set(SETTING_KEYS "")
foreach (variable ${ALL_VARIABLES})
  if (variable MATCHES "^SETTING_")
    string(APPEND SETTING_KEYS "${variable}=${${variable}}\n")
  endif ()
endforeach ()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/setting_keys.txt "${SETTING_KEYS}")
```

- add `SETTING_KEYS_FILE="${CMAKE_CURRENT_BINARY_DIR}/setting_keys.txt"` to `target_compile_definitions`

- [ ] **Step 3: Run to verify it fails**

Run: `cmake -B build-tests -DBUILD_TESTING=ON && cmake --build build-tests --target reflex-tests`
Expected: build error, `settings_schema.h` not found.

- [ ] **Step 4: Implement**

`src/settings_schema.h`:

```c
#ifndef SETTINGS_SCHEMA_H
#define SETTINGS_SCHEMA_H
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    SET_BOOL,      // true/false
    SET_CHOICE,    // one of `choices`
    SET_INT,       // whole number in [min, max]
    SET_PERCENT,   // "N%" with N in [min, max]
    SET_LENGTH,    // "N%" (0-100) or whole pixels in [0, max]
    SET_DECIMAL,   // number with optional fraction in [min, max]
    SET_COLOR,     // "#RRGGBB"
    SET_TEXT,      // any non-empty single-line text
    SET_FILE,      // path to a file
    SET_DIR,       // path to a directory
    SET_FONT,      // path to a TTF font
    SET_COMMAND,   // a command or :special command
    SET_MENU_REF   // name of a menu section
} SettingType;

typedef struct {
    const char *id;            // SETTING_* macro name, e.g. "SETTING_VSYNC"
    const char *section;       // INI section, e.g. "General"
    const char *key;           // INI key, e.g. "VSync"
    const char *label;         // UI label
    const char *help;          // one-line UI help
    SettingType type;
    int min;
    int max;
    const char *const *choices;  // SET_CHOICE, NULL-terminated
    const char *default_value;   // shipped sample value, or NULL if the sample leaves it unset
} SettingDef;

extern const SettingDef settings_schema[];
extern const size_t settings_schema_count;
extern const char *const schema_sections[];

const SettingDef *schema_find(const char *section, const char *key);
size_t schema_section_settings(const char *section, const SettingDef **out, size_t max);
bool schema_validate(const SettingDef *def, const char *value, char *err, size_t err_len);

#endif
```

`src/settings_schema.c`:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <SDL.h>
#include "launcher.h"
#include <launcher_config.h>
#include "util.h"
#include "settings_schema.h"

static bool fail(char *err, size_t err_len, const char *format, ...);
static bool parse_int_strict(const char *s, long *out);
static bool percent_in_range(const char *value, int min, int max);

#define S(section, id, type, min, max, choices, def, label, help) \
    { #id, section, id, label, help, type, min, max, choices, def }
#define NO_RANGE 0, 0
#define CHOICES(mode) ((const char *const *) mode_settings[mode])

const char *const schema_sections[] = {
    "General", "Background", "Layout", "Titles", "Highlight",
    "Scroll Indicators", "Clock", "Screensaver", "Gamepad", NULL
};

const SettingDef settings_schema[] = {
    // General
    S("General", SETTING_DEFAULT_MENU, SET_MENU_REF, NO_RANGE, NULL, "Main", "Home menu", "Menu shown at startup and by :home"),
    S("General", SETTING_VSYNC, SET_BOOL, NO_RANGE, NULL, "true", "VSync", "Sync drawing to the display refresh rate"),
    S("General", SETTING_FPS_LIMIT, SET_INT, MIN_FPS_LIMIT + 1, 1000, NULL, NULL, "FPS limit", "Frame rate cap when VSync is off"),
    S("General", SETTING_APPLICATION_TIMEOUT, SET_INT, MIN_APPLICATION_TIMEOUT, MAX_APPLICATION_TIMEOUT, NULL, "15", "Launch timeout (s)", "How long to wait for a launched app to take focus"),
    S("General", SETTING_ON_LAUNCH, SET_CHOICE, NO_RANGE, CHOICES(MODE_SETTING_ON_LAUNCH), "Blank", "When an app launches", "Blank the screen, do nothing, or quit the launcher"),
    S("General", SETTING_WRAP_ENTRIES, SET_BOOL, NO_RANGE, NULL, "false", "Wrap around", "Moving past the last entry returns to the first"),
    S("General", SETTING_RESET_ON_BACK, SET_BOOL, NO_RANGE, NULL, "false", "Reset on back", "Return to the first entry when going back a menu"),
    S("General", SETTING_MOUSE_SELECT, SET_BOOL, NO_RANGE, NULL, "false", "Mouse select", "Left click launches the highlighted entry"),
    S("General", SETTING_INHIBIT_OS_SCREENSAVER, SET_BOOL, NO_RANGE, NULL, "true", "Block OS screensaver", "Stop the system screensaver while the launcher is open"),
    S("General", SETTING_STARTUP_CMD, SET_COMMAND, NO_RANGE, NULL, NULL, "Startup command", "Runs once when the launcher starts"),
    S("General", SETTING_QUIT_CMD, SET_COMMAND, NO_RANGE, NULL, NULL, "Quit command", "Runs when the launcher quits"),

    // Background
    S("Background", SETTING_BACKGROUND_MODE, SET_CHOICE, NO_RANGE, CHOICES(MODE_SETTING_BACKGROUND), "Color", "Background", "Solid color, image, slideshow, or transparent"),
    S("Background", SETTING_BACKGROUND_COLOR, SET_COLOR, NO_RANGE, NULL, "#000000", "Color", "Background color in Color mode"),
    S("Background", SETTING_BACKGROUND_IMAGE, SET_FILE, NO_RANGE, NULL, NULL, "Image", "Background image in Image mode"),
    S("Background", SETTING_SLIDESHOW_DIRECTORY, SET_DIR, NO_RANGE, NULL, NULL, "Slideshow folder", "Folder of images for Slideshow mode"),
    S("Background", SETTING_SLIDESHOW_IMAGE_DURATION, SET_INT, MIN_SLIDESHOW_IMAGE_DURATION / 1000, MAX_SLIDESHOW_IMAGE_DURATION / 1000, NULL, "30", "Seconds per image", "How long each slideshow image shows"),
    S("Background", SETTING_SLIDESHOW_TRANSITION_TIME, SET_DECIMAL, 0, MAX_SLIDESHOW_TRANSITION_TIME / 1000, NULL, "3", "Transition (s)", "Crossfade time between slideshow images"),
    S("Background", SETTING_CHROMA_KEY_COLOR, SET_COLOR, NO_RANGE, NULL, "#010101", "Chroma key", "Color made see-through in Transparent mode (Windows)"),
    S("Background", SETTING_BACKGROUND_OVERLAY, SET_BOOL, NO_RANGE, NULL, "false", "Overlay", "Tint the background with a color"),
    S("Background", SETTING_BACKGROUND_OVERLAY_COLOR, SET_COLOR, NO_RANGE, NULL, "#000000", "Overlay color", "Tint color"),
    S("Background", SETTING_BACKGROUND_OVERLAY_OPACITY, SET_PERCENT, 0, 100, NULL, "50%", "Overlay opacity", "Tint strength"),

    // Layout
    S("Layout", SETTING_MAX_BUTTONS, SET_INT, 1, 50, NULL, "4", "Icons per page", "Maximum entries shown at once"),
    S("Layout", SETTING_ICON_SIZE, SET_INT, MIN_ICON_SIZE, MAX_ICON_SIZE, NULL, "256", "Icon size (px)", "Width and height of each icon"),
    S("Layout", SETTING_ICON_SPACING, SET_LENGTH, 0, 10000, NULL, "5%", "Icon spacing", "Gap between icons, in % of screen width or pixels"),
    S("Layout", SETTING_VCENTER, SET_PERCENT, (int) (MIN_VCENTER * 100), (int) (MAX_VCENTER * 100), NULL, "50%", "Vertical position", "Where the icon row sits, as % of screen height"),

    // Titles
    S("Titles", SETTING_TITLES_ENABLED, SET_BOOL, NO_RANGE, NULL, "true", "Show titles", "Show a title under each icon"),
    S("Titles", SETTING_TITLE_FONT, SET_FONT, NO_RANGE, NULL, NULL, "Font", "TTF font for titles"),
    S("Titles", SETTING_TITLE_FONT_SIZE, SET_INT, 8, 300, NULL, "36", "Font size", "Title text size in points"),
    S("Titles", SETTING_TITLE_FONT_COLOR, SET_COLOR, NO_RANGE, NULL, "#FFFFFF", "Color", "Title text color"),
    S("Titles", SETTING_TITLE_OPACITY, SET_PERCENT, 0, 100, NULL, "100%", "Opacity", "Title text opacity"),
    S("Titles", SETTING_TITLE_SHADOWS, SET_BOOL, NO_RANGE, NULL, "false", "Shadows", "Drop shadow behind title text"),
    S("Titles", SETTING_TITLE_SHADOW_COLOR, SET_COLOR, NO_RANGE, NULL, "#000000", "Shadow color", "Title shadow color"),
    S("Titles", SETTING_TITLE_OVERSIZE_MODE, SET_CHOICE, NO_RANGE, CHOICES(MODE_SETTING_OVERSIZE), "Shrink", "Long titles", "Truncate, shrink, or leave titles wider than the icon"),
    S("Titles", SETTING_TITLE_PADDING, SET_INT, 0, 500, NULL, "20", "Padding (px)", "Space between icon and title"),

    // Highlight
    S("Highlight", SETTING_HIGHLIGHT_ENABLED, SET_BOOL, NO_RANGE, NULL, "true", "Show highlight", "Draw a box behind the selected entry"),
    S("Highlight", SETTING_HIGHLIGHT_FILL_COLOR, SET_COLOR, NO_RANGE, NULL, "#FFFFFF", "Fill color", "Highlight box color"),
    S("Highlight", SETTING_HIGHLIGHT_FILL_OPACITY, SET_PERCENT, 0, 100, NULL, "25%", "Fill opacity", "Highlight box opacity"),
    S("Highlight", SETTING_HIGHLIGHT_OUTLINE_SIZE, SET_INT, 0, 100, NULL, "0", "Outline (px)", "Highlight border width; 0 for none"),
    S("Highlight", SETTING_HIGHLIGHT_OUTLINE_COLOR, SET_COLOR, NO_RANGE, NULL, "#0000FF", "Outline color", "Highlight border color"),
    S("Highlight", SETTING_HIGHLIGHT_OUTLINE_OPACITY, SET_PERCENT, 0, 100, NULL, "100%", "Outline opacity", "Highlight border opacity"),
    S("Highlight", SETTING_HIGHLIGHT_CORNER_RADIUS, SET_INT, MIN_RX_SIZE, MAX_RX_SIZE, NULL, "0", "Corner radius", "Rounded corners on the highlight box"),
    S("Highlight", SETTING_HIGHLIGHT_VPADDING, SET_INT, 0, 500, NULL, "30", "Vertical padding (px)", "Space above and below the entry"),
    S("Highlight", SETTING_HIGHLIGHT_HPADDING, SET_INT, 0, 500, NULL, "30", "Horizontal padding (px)", "Space left and right of the entry"),

    // Scroll Indicators
    S("Scroll Indicators", SETTING_SCROLL_INDICATORS, SET_BOOL, NO_RANGE, NULL, "true", "Show arrows", "Arrows when more entries are off screen"),
    S("Scroll Indicators", SETTING_SCROLL_INDICATOR_FILL_COLOR, SET_COLOR, NO_RANGE, NULL, "#FFFFFF", "Color", "Arrow color"),
    S("Scroll Indicators", SETTING_SCROLL_INDICATOR_OUTLINE_SIZE, SET_INT, 0, 100, NULL, "0", "Outline (px)", "Arrow border width; 0 for none"),
    S("Scroll Indicators", SETTING_SCROLL_INDICATOR_OUTLINE_COLOR, SET_COLOR, NO_RANGE, NULL, "#000000", "Outline color", "Arrow border color"),
    S("Scroll Indicators", SETTING_SCROLL_INDICATOR_OPACITY, SET_PERCENT, 0, 100, NULL, "100%", "Opacity", "Arrow opacity"),

    // Clock
    S("Clock", SETTING_CLOCK_ENABLED, SET_BOOL, NO_RANGE, NULL, "false", "Show clock", "Show the time in a corner"),
    S("Clock", SETTING_CLOCK_SHOW_DATE, SET_BOOL, NO_RANGE, NULL, "false", "Show date", "Show the date under the time"),
    S("Clock", SETTING_CLOCK_ALIGNMENT, SET_CHOICE, NO_RANGE, CHOICES(MODE_SETTING_ALIGNMENT), "Left", "Corner", "Top-left or top-right"),
    S("Clock", SETTING_CLOCK_FONT, SET_FONT, NO_RANGE, NULL, NULL, "Font", "TTF font for the clock"),
    S("Clock", SETTING_CLOCK_FONT_SIZE, SET_INT, 8, 300, NULL, "50", "Font size", "Clock text size in points"),
    S("Clock", SETTING_CLOCK_FONT_COLOR, SET_COLOR, NO_RANGE, NULL, "#FFFFFF", "Color", "Clock text color"),
    S("Clock", SETTING_CLOCK_SHADOWS, SET_BOOL, NO_RANGE, NULL, "false", "Shadows", "Drop shadow behind the clock"),
    S("Clock", SETTING_CLOCK_SHADOW_COLOR, SET_COLOR, NO_RANGE, NULL, "#000000", "Shadow color", "Clock shadow color"),
    S("Clock", SETTING_CLOCK_MARGIN, SET_LENGTH, 0, 10000, NULL, "5%", "Margin", "Distance from the screen edge, in % or pixels"),
    S("Clock", SETTING_CLOCK_OPACITY, SET_PERCENT, 0, 100, NULL, "100%", "Opacity", "Clock text opacity"),
    S("Clock", SETTING_CLOCK_TIME_FORMAT, SET_CHOICE, NO_RANGE, CHOICES(MODE_SETTING_TIME_FORMAT), "Auto", "Time format", "24-hour, 12-hour, or from your region"),
    S("Clock", SETTING_CLOCK_DATE_FORMAT, SET_CHOICE, NO_RANGE, CHOICES(MODE_SETTING_DATE_FORMAT), "Auto", "Date format", "Month first, day first, or from your region"),
    S("Clock", SETTING_CLOCK_INCLUDE_WEEKDAY, SET_BOOL, NO_RANGE, NULL, "true", "Weekday", "Include the day of the week in the date"),

    // Screensaver
    S("Screensaver", SETTING_SCREENSAVER_ENABLED, SET_BOOL, NO_RANGE, NULL, "false", "Dim when idle", "Fade the screen after a period of no input"),
    S("Screensaver", SETTING_SCREENSAVER_IDLE_TIME, SET_INT, MIN_SCREENSAVER_IDLE_TIME, MAX_SCREENSAVER_IDLE_TIME, NULL, "300", "Idle time (s)", "Seconds of no input before dimming"),
    S("Screensaver", SETTING_SCREENSAVER_INTENSITY, SET_PERCENT, 1, 100, NULL, "70%", "Dim amount", "How dark the screen gets"),
    S("Screensaver", SETTING_SCREENSAVER_PAUSE_SLIDESHOW, SET_BOOL, NO_RANGE, NULL, "true", "Pause slideshow", "Stop changing images while dimmed"),

    // Gamepad
    S("Gamepad", SETTING_GAMEPAD_ENABLED, SET_BOOL, NO_RANGE, NULL, "false", "Gamepad", "Control the launcher with a gamepad"),
    S("Gamepad", SETTING_GAMEPAD_DEVICE, SET_INT, -1, 15, NULL, "-1", "Device", "Which gamepad to use; -1 for any"),
    S("Gamepad", SETTING_GAMEPAD_MAPPINGS_FILE, SET_FILE, NO_RANGE, NULL, NULL, "Mappings file", "SDL gamecontrollerdb.txt for unrecognized pads"),
    S("Gamepad", SETTING_GAMEPAD_LSTICK_XM, SET_COMMAND, NO_RANGE, NULL, ":left", "Left stick left", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_LSTICK_XP, SET_COMMAND, NO_RANGE, NULL, ":right", "Left stick right", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_LSTICK_YM, SET_COMMAND, NO_RANGE, NULL, NULL, "Left stick up", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_LSTICK_YP, SET_COMMAND, NO_RANGE, NULL, NULL, "Left stick down", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_RSTICK_XM, SET_COMMAND, NO_RANGE, NULL, NULL, "Right stick left", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_RSTICK_XP, SET_COMMAND, NO_RANGE, NULL, NULL, "Right stick right", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_RSTICK_YM, SET_COMMAND, NO_RANGE, NULL, NULL, "Right stick up", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_RSTICK_YP, SET_COMMAND, NO_RANGE, NULL, NULL, "Right stick down", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_LTRIGGER, SET_COMMAND, NO_RANGE, NULL, NULL, "Left trigger", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_RTRIGGER, SET_COMMAND, NO_RANGE, NULL, NULL, "Right trigger", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_BUTTON_A, SET_COMMAND, NO_RANGE, NULL, ":select", "A button", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_BUTTON_B, SET_COMMAND, NO_RANGE, NULL, ":back", "B button", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_BUTTON_X, SET_COMMAND, NO_RANGE, NULL, NULL, "X button", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_BUTTON_Y, SET_COMMAND, NO_RANGE, NULL, NULL, "Y button", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_BUTTON_BACK, SET_COMMAND, NO_RANGE, NULL, NULL, "Back button", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_BUTTON_GUIDE, SET_COMMAND, NO_RANGE, NULL, NULL, "Guide button", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_BUTTON_START, SET_COMMAND, NO_RANGE, NULL, NULL, "Start button", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_BUTTON_LEFT_STICK, SET_COMMAND, NO_RANGE, NULL, NULL, "Left stick press", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_BUTTON_RIGHT_STICK, SET_COMMAND, NO_RANGE, NULL, NULL, "Right stick press", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_BUTTON_LEFT_SHOULDER, SET_COMMAND, NO_RANGE, NULL, NULL, "Left shoulder", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_BUTTON_RIGHT_SHOULDER, SET_COMMAND, NO_RANGE, NULL, NULL, "Right shoulder", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_BUTTON_DPAD_UP, SET_COMMAND, NO_RANGE, NULL, NULL, "D-pad up", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_BUTTON_DPAD_DOWN, SET_COMMAND, NO_RANGE, NULL, NULL, "D-pad down", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_BUTTON_DPAD_LEFT, SET_COMMAND, NO_RANGE, NULL, ":left", "D-pad left", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_BUTTON_DPAD_RIGHT, SET_COMMAND, NO_RANGE, NULL, ":right", "D-pad right", "Command for this input"),
};

const size_t settings_schema_count = sizeof(settings_schema) / sizeof(settings_schema[0]);

static bool fail(char *err, size_t err_len, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(err, err_len, format, args);
    va_end(args);
    return false;
}

static bool parse_int_strict(const char *s, long *out)
{
    if (*s == '\0')
        return false;
    char *end;
    errno = 0;
    long value = strtol(s, &end, 10);
    if (*end != '\0' || errno != 0)
        return false;
    *out = value;
    return true;
}

static bool percent_in_range(const char *value, int min, int max)
{
    if (!is_percent(value))
        return false;
    char number[PERCENT_MAX_CHARS];
    copy_string(number, value, sizeof(number));
    number[strlen(number) - 1] = '\0';
    long n;
    return parse_int_strict(number, &n) && n >= min && n <= max;
}

const SettingDef *schema_find(const char *section, const char *key)
{
    for (size_t i = 0; i < settings_schema_count; i++) {
        if (strcmp(settings_schema[i].section, section) == 0 && strcmp(settings_schema[i].key, key) == 0)
            return &settings_schema[i];
    }
    return NULL;
}

size_t schema_section_settings(const char *section, const SettingDef **out, size_t max)
{
    size_t count = 0;
    for (size_t i = 0; i < settings_schema_count && count < max; i++) {
        if (strcmp(settings_schema[i].section, section) == 0)
            out[count++] = &settings_schema[i];
    }
    return count;
}

bool schema_validate(const SettingDef *def, const char *value, char *err, size_t err_len)
{
    long n;
    bool b;
    switch (def->type) {
        case SET_BOOL:
            return convert_bool(value, &b) ? true : fail(err, err_len, "%s must be true or false", def->label);

        case SET_CHOICE:
            for (size_t i = 0; def->choices[i] != NULL; i++) {
                if (strcmp(def->choices[i], value) == 0)
                    return true;
            }
            return fail(err, err_len, "%s has an unknown option \"%s\"", def->label, value);

        case SET_INT:
            if (parse_int_strict(value, &n) && n >= def->min && n <= def->max)
                return true;
            return fail(err, err_len, "%s must be a whole number from %d to %d", def->label, def->min, def->max);

        case SET_PERCENT:
            if (percent_in_range(value, def->min, def->max))
                return true;
            return fail(err, err_len, "%s must be a percentage from %d%% to %d%%", def->label, def->min, def->max);

        case SET_LENGTH:
            if (percent_in_range(value, 0, 100) || (parse_int_strict(value, &n) && n >= 0 && n <= def->max))
                return true;
            return fail(err, err_len, "%s must be a percentage or a pixel count", def->label);

        case SET_DECIMAL: {
            char *end;
            errno = 0;
            double d = strtod(value, &end);
            if (*value != '\0' && *end == '\0' && errno == 0 && d >= def->min && d <= def->max)
                return true;
            return fail(err, err_len, "%s must be a number from %d to %d", def->label, def->min, def->max);
        }

        case SET_COLOR: {
            bool hex = value[0] == '#' && strlen(value) == 7;
            for (size_t i = 1; hex && i < 7; i++)
                hex = isxdigit((unsigned char) value[i]) != 0;
            return hex ? true : fail(err, err_len, "%s must be a color like #1A2B3C", def->label);
        }

        case SET_TEXT:
        case SET_FILE:
        case SET_DIR:
        case SET_FONT:
        case SET_COMMAND:
        case SET_MENU_REF:
            return value[0] != '\0' ? true : fail(err, err_len, "%s can't be empty", def->label);
    }
    return fail(err, err_len, "%s has an unknown type", def->label);
}
```

Add `settings_schema.c` and `settings_schema.h` to `SOURCES` in `src/CMakeLists.txt` so the app compiles them too, ready for sub-project 2.

- [ ] **Step 5: Run the tests**

Run: `cmake -B build-tests -DBUILD_TESTING=ON && cmake --build build-tests --target reflex-tests && ctest --test-dir build-tests --output-on-failure`
Expected: PASS for `ini_doc` and `schema`. If `test_defaults_match_shipped_sample` reports a mismatch, the **sample is the truth**: fix the table's `default_value`, and record the case in the commit message. If `test_every_setting_macro_is_in_the_table_once` reports a missing macro, add a row for it.

- [ ] **Step 6: Commit**

```bash
git add src/settings_schema.h src/settings_schema.c src/util.c src/util.h src/CMakeLists.txt tests/
git commit -m "Add settings schema covering all scalar config.ini settings"
```

---

### Task 7: `default_config` extraction, resettable parser, parser-recognition test

**Files:**
- Create: `src/config_defaults.c`
- Modify: `src/launcher.c:47-157` (remove the initializer), `src/launcher.h` (Config field, extern), `src/util.c` (parser state), `src/util.h`, `src/CMakeLists.txt`, `tests/test_schema.c`, `tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `extern const Config default_config;` (`launcher.h`)
  - `Config.config_path` (`char*`)
  - `void reset_config_parser(void)`
  - `int parse_config_file(const char *config_file_path)`: 0 on success
- Consumes: Task 6's schema.

- [ ] **Step 1: Write the failing tests**

Add to `tests/test_schema.c`:

```c
#include <ini.h>
#include "util.h"

extern Config config;
extern Hotkey *hotkeys;
extern GamepadControl *gamepad_controls;

static void parse_text(const char *text)
{
    reset_config_parser();
    ini_parse_string(text, config_handler, NULL);
}

// The parser must recognize every key in the table: some schema-valid value must change parser state
static void test_parser_recognizes_every_schema_key(void)
{
    static const char *candidates[] = {
        "true", "false", "#123456", "37%", "12", "1.5", "probe", ":left",
        "Color", "Image", "Slideshow", "Transparent", "Blank", "None", "Quit", "Truncated",
        "Shrink", "Left", "Right", "24hr", "12hr", "Auto", "Big", "Little", NULL
    };
    char err[256], text[512], number[16];
    for (size_t i = 0; i < settings_schema_count; i++) {
        const SettingDef *def = &settings_schema[i];
        bool tried = false, recognized = false;
        for (int c = -2; !recognized && (c < 0 || candidates[c] != NULL); c++) {
            const char *value;
            if (c < 0) {   // integer bounds first
                snprintf(number, sizeof(number), "%d", c == -2 ? def->min : def->max);
                value = number;
            }
            else
                value = candidates[c];
            if (!schema_validate(def, value, err, sizeof(err)))
                continue;
            tried = true;
            memcpy(&config, &default_config, sizeof(Config));
            hotkeys = NULL;
            gamepad_controls = NULL;
            snprintf(text, sizeof(text), "[%s]\n%s=%s\n", def->section, def->key, value);
            parse_text(text);
            recognized = memcmp(&config, &default_config, sizeof(Config)) != 0 || gamepad_controls != NULL;
        }
        if (!tried || !recognized)
            fprintf(stderr, "  [%s] %s not recognized by config_handler\n", def->section, def->key);
        CHECK(tried && recognized);
    }
}

// Parsing twice must start fresh lists (the parser used to keep static "current" pointers)
static void test_parser_state_resets_between_parses(void)
{
    for (int round = 0; round < 2; round++) {
        memcpy(&config, &default_config, sizeof(Config));
        hotkeys = NULL;
        gamepad_controls = NULL;
        parse_text("[Hotkeys]\nHotkey1=#1B;:quit\nHotkey2=#72;:reload\n"
                   "[Gamepad]\nButtonA=:select\n"
                   "[Main]\nEntry1=A;a.png;cmd\n");
        size_t count = 0;
        for (Hotkey *h = hotkeys; h != NULL; h = h->next)
            count++;
        CHECK(count == 2);
        CHECK(gamepad_controls != NULL && gamepad_controls->next == NULL);
        CHECK(config.num_menus == 1 && config.first_menu->num_entries == 1);
    }
}
```

Call both from `suite_schema()`. In `tests/CMakeLists.txt`, add `${PROJECT_SOURCE_DIR}/src/config_defaults.c` to `TEST_SOURCES`.

These tests leak the parsed lists. That's acceptable in a test process, and it's why they reset the list heads by hand.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build-tests --target reflex-tests`
Expected: build errors. `default_config`, `reset_config_parser`, and `config_defaults.c` don't exist yet.

- [ ] **Step 3: Move the defaults**

1. Create `src/config_defaults.c` with this header:

```c
// Built-in default settings, used before config.ini is parsed and on every reload
#include <stdbool.h>
#include <SDL.h>
#include "launcher.h"
#include <launcher_config.h>

const Config default_config = {
```

followed by the **exact body** of the current initializer in `src/launcher.c:48-157` (from `.default_menu = NULL,` through `.slideshow_transition_time = DEFAULT_SLIDESHOW_TRANSITION_TIME`), plus one added line, `.config_path = NULL,`, then `};`.

2. In `src/launcher.c`, replace the whole `Config config = { ... };` block (lines 47-157) with:

```c
Config config;
```

3. In `src/launcher.h`, add `char *config_path;` after `char *exe_path;` in `Config`, and after the `Config` typedef add:

```c
extern const Config default_config;
```

4. In `src/CMakeLists.txt`, add `config_defaults.c` to `SOURCES`.

5. At the top of `main()` in `src/launcher.c`, before `config.exe_path = SDL_GetBasePath();`, add:

```c
    memcpy(&config, &default_config, sizeof(Config));
```

- [ ] **Step 4: Make the parser resettable**

In `src/util.c`:

1. Below the existing `Menu *menu = NULL;` / `Entry *entry = NULL;` globals, add:

```c
static Hotkey *current_hotkey = NULL;
static GamepadControl *current_gamepad_control = NULL;
```

2. Delete the `static Hotkey *current_hotkey = NULL;` line inside `add_hotkey()` (~line 778) and the `static GamepadControl *current_gamepad_control = NULL;` line inside `add_gamepad_control()` (~line 848). Both functions now use the file-level variables.

3. Add:

```c
// A function to reset parser state so the config file can be parsed again
void reset_config_parser()
{
    menu = NULL;
    entry = NULL;
    current_hotkey = NULL;
    current_gamepad_control = NULL;
}
```

4. Replace `parse_config_file` with:

```c
// A function to parse the config file and store the settings into the config struct
int parse_config_file(const char *config_file_path)
{
    reset_config_parser();
    FILE *file = fopen(config_file_path, "r");
    if (file == NULL) {
        log_error("Could not open config file %s", config_file_path);
        return 1;
    }
    int error = ini_parse_file(file, config_handler, NULL);
    fclose(file);
    if (error < 0) {
        log_error("Could not parse config file %s", config_file_path);
        return 1;
    }
    return 0;
}
```

5. In `src/util.h`, change the prototype to `int parse_config_file(const char *config_file_path);` and add `void reset_config_parser(void);`.

6. In `main()` (`src/launcher.c`), keep the current behavior for now by replacing `parse_config_file(config_file_path);` with:

```c
    if (parse_config_file(config_file_path))
        log_fatal("Could not load config file");
```

Task 9 restructures `main()` fully.

- [ ] **Step 5: Run the tests and the app build**

Run: `cmake --build build-tests --target reflex-tests && ctest --test-dir build-tests --output-on-failure`
Expected: PASS for all suites.

Run: `cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build && ./build/flex-launcher -v`
Expected: builds and prints the version.

- [ ] **Step 6: Commit**

```bash
git add src/config_defaults.c src/launcher.c src/launcher.h src/util.c src/util.h src/CMakeLists.txt tests/
git commit -m "Move built-in defaults to config_defaults.c and make the parser resettable"
```

---

### Task 8: `config_check` and `config_save`

**Files:**
- Create: `src/config_io.h`, `src/config_io.c`, `tests/test_config_io.c`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/test_main.c`

**Interfaces:**
- Consumes:
  - Task 5's `ini_doc_*` save, dirty, and change-detection functions
  - Task 6's `schema_find`, `schema_validate`, `schema_sections`
- Produces:
  - `typedef enum ConfigSaveResult`: `CONFIG_SAVE_OK`, `CONFIG_SAVE_INVALID`, `CONFIG_SAVE_CHANGED_ON_DISK`, `CONFIG_SAVE_WRITE_FAILED`
  - `bool config_check(const IniDoc *doc, char *err, size_t err_len)`
  - `bool config_entry_is_valid(const char *value)`
  - `ConfigSaveResult config_save(IniDoc *doc, const char *path, bool force, char *err, size_t err_len)`: on success the doc's dirty set is cleared and its disk snapshot refreshed. It does **not** reload; the caller calls `launcher_reload()`.

- [ ] **Step 1: Write the failing tests**

`tests/test_config_io.c`:

```c
#include <stdlib.h>
#include "test.h"
#include "ini_doc.h"
#include "config_io.h"

static const char *good =
    "[General]\nDefaultMenu=Main\n"
    "[Hotkeys]\nHotkey1=#1B;:quit\n"
    "[Main]\nEntry1=Kodi;kodi.png;open -a Kodi\nEntry2=System;system.png;:submenu System\n"
    "[System]\nEntry1=Sleep;sleep.png;:sleep\n";

static void check_config(const char *text, bool expected, const char *message_part)
{
    char err[512] = "";
    IniDoc *doc = ini_doc_parse(text);
    bool ok = config_check(doc, err, sizeof(err));
    if (ok != expected || (!ok && strstr(err, message_part) == NULL))
        fprintf(stderr, "  config_check -> %d \"%s\" (expected %d, \"%s\")\n", ok, err, expected, message_part);
    CHECK(ok == expected);
    if (!expected)
        CHECK(strstr(err, message_part) != NULL);
    ini_doc_free(doc);
}

static void test_config_check(void)
{
    check_config(good, true, "");
    check_config("[Main]\nEntry1=A;a;a\n", false, "default menu");
    check_config("[General]\nDefaultMenu=Home\n[Main]\nEntry1=A;a;a\n", false, "\"Home\" does not exist");
    check_config("[General]\nDefaultMenu=Main\n[Main]\n", false, "has no entries");
    check_config("[General]\nDefaultMenu=Main\n[Main]\nEntry1=OnlyTitle\n", false, "Entry 1 in [Main]");
    check_config("[General]\nDefaultMenu=Main\n[Main]\nEntry1=T;;cmd\n", false, "Entry 1 in [Main]");
    check_config("[General]\nDefaultMenu=Main\n[Main]\nEntry1=T;i;:submenu Nope\n", false, "\"Nope\"");
    check_config("[General]\nDefaultMenu=Main\n[Main]\nEntry1=T;i;c\n[Hotkeys]\nHotkey1=1B;:quit\n", false, "Hotkey 1");
}

static void test_entry_validity_mirrors_parser(void)
{
    CHECK(config_entry_is_valid("Title;icon.png;cmd"));
    CHECK(config_entry_is_valid("Title;icon.png;cmd;with;semicolons"));
    CHECK(!config_entry_is_valid("Title;icon.png"));
    CHECK(!config_entry_is_valid("Title;icon.png;:select"));   // parser drops these
    CHECK(!config_entry_is_valid(";;"));
}

static void test_config_save(void)
{
    char path[512], err[512];
    test_tmp_path(path, sizeof(path), "save-test.ini");
    test_write_file(path, good);

    // invalid edited value -> nothing written
    IniDoc *doc = ini_doc_load(path);
    ini_doc_set(doc, "Layout", "IconSize", "5000");
    CHECK(config_save(doc, path, false, err, sizeof(err)) == CONFIG_SAVE_INVALID);
    CHECK(strstr(err, "Icon size") != NULL);
    char *unchanged = test_read_file(path);
    CHECK_STR(unchanged, good);
    free(unchanged);

    // a pre-existing bad value that wasn't edited never blocks saving
    ini_doc_free(doc);
    char with_bad[1024];
    snprintf(with_bad, sizeof(with_bad), "%s[Clock]\nOpacity=lots\n", good);
    test_write_file(path, with_bad);
    doc = ini_doc_load(path);
    ini_doc_set(doc, "General", "VSync", "false");
    CHECK(config_save(doc, path, false, err, sizeof(err)) == CONFIG_SAVE_OK);
    CHECK(ini_doc_dirty_count(doc) == 0);

    // too-long line (inih reads at most 199 bytes per line)
    char long_cmd[300];
    memset(long_cmd, 'x', sizeof(long_cmd) - 1);
    long_cmd[sizeof(long_cmd) - 1] = '\0';
    ini_doc_set(doc, "General", "StartupCmd", long_cmd);
    CHECK(config_save(doc, path, false, err, sizeof(err)) == CONFIG_SAVE_INVALID);
    CHECK(strstr(err, "too long") != NULL);
    ini_doc_unset(doc, "General", "StartupCmd");

    // file changed by someone else -> refuse unless forced
    test_write_file(path, "[General]\nDefaultMenu=Main\n[Main]\nEntry1=A;a;a\n# edited elsewhere\n");
    ini_doc_set(doc, "General", "VSync", "true");
    CHECK(config_save(doc, path, false, err, sizeof(err)) == CONFIG_SAVE_CHANGED_ON_DISK);
    CHECK(config_save(doc, path, true, err, sizeof(err)) == CONFIG_SAVE_OK);
    ini_doc_free(doc);

    // unwritable target
    char missing[512];
    test_tmp_path(missing, sizeof(missing), "no/such/dir/config.ini");
    doc = ini_doc_parse(good);
    CHECK(config_save(doc, missing, false, err, sizeof(err)) == CONFIG_SAVE_WRITE_FAILED);
    CHECK(strstr(err, "Could not write") != NULL);
    ini_doc_free(doc);
}

void suite_config_io(void)
{
    test_config_check();
    test_entry_validity_mirrors_parser();
    test_config_save();
}
```

Register `{"config_io", suite_config_io}` in `tests/test_main.c` (with its declaration). In `tests/CMakeLists.txt`, add `test_config_io.c` and `${PROJECT_SOURCE_DIR}/src/config_io.c` to `TEST_SOURCES`, and set `TEST_SUITES` to `ini_doc schema config_io`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake -B build-tests -DBUILD_TESTING=ON && cmake --build build-tests --target reflex-tests`
Expected: build error, `config_io.h` not found.

- [ ] **Step 3: Implement**

`src/config_io.h`:

```c
#ifndef CONFIG_IO_H
#define CONFIG_IO_H
#include <stdbool.h>
#include <stddef.h>
#include "ini_doc.h"

typedef enum {
    CONFIG_SAVE_OK = 0,
    CONFIG_SAVE_INVALID,          // a changed value or the menu structure is invalid; nothing written
    CONFIG_SAVE_CHANGED_ON_DISK,  // another program edited the file since it was loaded
    CONFIG_SAVE_WRITE_FAILED      // the file couldn't be written; nothing changed
} ConfigSaveResult;

bool config_check(const IniDoc *doc, char *err, size_t err_len);
bool config_entry_is_valid(const char *value);
ConfigSaveResult config_save(IniDoc *doc, const char *path, bool force, char *err, size_t err_len);

#endif
```

`src/config_io.c`:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <ini.h>
#include "config_io.h"
#include "settings_schema.h"

// inih reads lines with fgets(INI_MAX_LINE): the text, CR/LF, and NUL must fit
#define MAX_LINE_CHARS (INI_MAX_LINE - 3)

#ifdef _WIN32
#define USER_CONFIG_HINT "the folder that contains flex-launcher.exe"
#else
#define USER_CONFIG_HINT "~/.config/flex-launcher/config.ini"
#endif

static bool fail(char *err, size_t err_len, const char *format, ...);
static bool is_menu_section(const char *section);
static char *entry_command(const char *value);
static bool hotkey_is_valid(const char *value);

static bool fail(char *err, size_t err_len, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(err, err_len, format, args);
    va_end(args);
    return false;
}

// Mirrors config_handler(): any section that isn't a settings section or [Hotkeys] is a menu
static bool is_menu_section(const char *section)
{
    if (section[0] == '\0' || strcmp(section, "Hotkeys") == 0)
        return false;
    for (const char *const *s = schema_sections; *s != NULL; s++) {
        if (strcmp(*s, section) == 0)
            return false;
    }
    return true;
}

// Mirrors config_handler()'s strtok parsing: the first two non-empty ';' tokens are the
// title and icon, the remainder is the command. Returns a malloc'd command or NULL.
static char *entry_command(const char *value)
{
    char *copy = strdup(value);
    char *title = strtok(copy, ";");
    char *icon = title != NULL ? strtok(NULL, ";") : NULL;
    char *cmd = icon != NULL ? strtok(NULL, "") : NULL;
    char *result = cmd != NULL ? strdup(cmd) : NULL;
    free(copy);
    return result;
}

bool config_entry_is_valid(const char *value)
{
    char *cmd = entry_command(value);
    bool valid = cmd != NULL && strcmp(cmd, ":select") != 0;
    free(cmd);
    return valid;
}

// Mirrors the [Hotkeys] branch of config_handler() and add_hotkey()
static bool hotkey_is_valid(const char *value)
{
    char *copy = strdup(value);
    char *keycode = strtok(copy, ";");
    char *cmd = keycode != NULL ? strtok(NULL, "") : NULL;
    bool valid = keycode != NULL && keycode[0] == '#' && keycode[1] != '\0' && cmd != NULL;
    free(copy);
    return valid;
}

bool config_check(const IniDoc *doc, char *err, size_t err_len)
{
    const char *default_menu = ini_doc_get(doc, "General", "DefaultMenu");
    if (default_menu == NULL)
        return fail(err, err_len, "No default menu is set ([General] DefaultMenu)");
    if (!ini_doc_has_section(doc, default_menu))
        return fail(err, err_len, "The default menu \"%s\" does not exist", default_menu);

    const char **sections = NULL;
    size_t section_count = ini_doc_sections(doc, &sections);
    bool ok = true;
    for (size_t s = 0; ok && s < section_count; s++) {
        const char *section = sections[s];
        const char **values = NULL;
        size_t count = ini_doc_get_list(doc, section, &values);
        if (strcmp(section, "Hotkeys") == 0) {
            for (size_t i = 0; ok && i < count; i++) {
                if (!hotkey_is_valid(values[i]))
                    ok = fail(err, err_len, "Hotkey %zu in [Hotkeys] must look like #1B;command", i + 1);
            }
        }
        else if (is_menu_section(section)) {
            if (count == 0 && strcmp(section, default_menu) == 0)
                ok = fail(err, err_len, "The default menu \"%s\" has no entries", section);
            for (size_t i = 0; ok && i < count; i++) {
                char *cmd = entry_command(values[i]);
                if (cmd == NULL || strcmp(cmd, ":select") == 0)
                    ok = fail(err, err_len, "Entry %zu in [%s] needs a title, an icon and a command", i + 1, section);
                else if (strncmp(cmd, ":submenu ", 9) == 0 && !ini_doc_has_section(doc, cmd + 9))
                    ok = fail(err, err_len, "Entry %zu in [%s] opens submenu \"%s\", which does not exist", i + 1, section, cmd + 9);
                free(cmd);
            }
        }
        free(values);
    }
    free(sections);
    return ok;
}

ConfigSaveResult config_save(IniDoc *doc, const char *path, bool force, char *err, size_t err_len)
{
    // Validate only what the user changed; untouched bad values are ignored by the parser as before
    for (size_t i = 0; i < ini_doc_dirty_count(doc); i++) {
        const char *section, *key;
        ini_doc_dirty_get(doc, i, &section, &key);
        const SettingDef *def = schema_find(section, key);
        if (def != NULL) {
            const char *value = ini_doc_get(doc, section, key);
            if (value == NULL)
                continue;
            if (!schema_validate(def, value, err, err_len))
                return CONFIG_SAVE_INVALID;
            if (strlen(key) + 1 + strlen(value) > MAX_LINE_CHARS) {
                fail(err, err_len, "%s is too long (max %d characters)", def->label, (int) (MAX_LINE_CHARS - strlen(key) - 1));
                return CONFIG_SAVE_INVALID;
            }
        }
        else {
            const char **values = NULL;
            size_t count = ini_doc_get_list(doc, section, &values);
            bool too_long = false;
            for (size_t j = 0; j < count && !too_long; j++)
                too_long = strlen(key) + 4 + strlen(values[j]) > MAX_LINE_CHARS;
            free(values);
            if (too_long) {
                fail(err, err_len, "An entry in [%s] is too long (max about %d characters)", section, (int) (MAX_LINE_CHARS - strlen(key) - 4));
                return CONFIG_SAVE_INVALID;
            }
        }
    }
    if (!config_check(doc, err, err_len))
        return CONFIG_SAVE_INVALID;
    if (!force && ini_doc_changed_on_disk(doc, path)) {
        fail(err, err_len, "%s was changed by another program since it was opened", path);
        return CONFIG_SAVE_CHANGED_ON_DISK;
    }
    if (ini_doc_save(doc, path) != 0) {
        fail(err, err_len, "Could not write %s. If it is a system-wide file, copy it to " USER_CONFIG_HINT " and edit that copy.", path);
        return CONFIG_SAVE_WRITE_FAILED;
    }
    ini_doc_mark_saved(doc, path);
    return CONFIG_SAVE_OK;
}
```

Add `config_io.c`, `config_io.h`, `ini_doc.c`, and `ini_doc.h` to `SOURCES` in `src/CMakeLists.txt`.

- [ ] **Step 4: Run the tests**

Run: `cmake --build build-tests --target reflex-tests && ctest --test-dir build-tests --output-on-failure`
Expected: PASS for `ini_doc`, `schema`, `config_io`. The app build still succeeds: `cmake --build build`.

- [ ] **Step 5: Commit**

```bash
git add src/config_io.h src/config_io.c src/CMakeLists.txt tests/
git commit -m "Add config_check and config_save (validate edits, atomic write)"
```

---

### Task 9: In-process reload (`launcher_load` / `launcher_unload` / `:reload`)

**Files:**
- Modify: `src/launcher.c`, `src/launcher.h`, `src/image.c:414`, `src/platform/win32.c`, `src/platform/platform.h`

**Interfaces:**
- Consumes:
  - Task 7's `parse_config_file()` → `int`, `reset_config_parser()`, `default_config`, `config.config_path`
  - Task 5's `ini_doc_load`, `ini_doc_save`, `replace_file`
- Produces:
  - `int launcher_reload(void)` (non-static, declared in `launcher.h`; the Config UI in sub-project 2 calls it after `config_save`)
  - `SCMD_RELOAD ":reload"`
  - `State.reload_pending`

This task is verified manually, because it needs a display and real SDL. Make each step below, then build.

- [ ] **Step 1: Declarations**

In `src/launcher.h`:
- add `#define SCMD_RELOAD ":reload"` after `SCMD_SLEEP`
- add `bool reload_pending;` to the end of the `State` struct
- add `int launcher_reload(void);` next to `void quit(int status);`

In `src/platform/platform.h`, inside the `#ifdef _WIN32` prototype block, add `void unregister_exit_hotkey(void);`. In `src/platform/win32.c`, after `register_exit_hotkey()`:

```c
// A function to release the exit hotkey before the config is reloaded
void unregister_exit_hotkey()
{
    if (exit_hotkey)
        UnregisterHotKey(wm_info.info.win.window, 1);
    exit_hotkey = 0;
}
```

In `src/image.c:414`, change `log_fatal("Could not load default font");` to `log_error("Could not load default font");`. The `return 1;` after it stays, and callers decide what's fatal.

- [ ] **Step 2: Split window and renderer creation**

In `src/launcher.c`, update the prototype list at the top:
- replace `static void init_sdl_ttf(void);` with `static int load_title_font(void);`
- replace `static void cleanup(void);` with `static void cleanup(void);`, `static int create_renderer(void);`, `static int launcher_load(void);`, `static void launcher_unload(void);`, `static void free_config_lists(void);`, and `static int restore_config_backup(const char *path);`

Replace `init_sdl()` with this version. The screensaver hint and the gamepad subsystem move to `launcher_load()`, because config isn't parsed yet when `init_sdl()` runs:

```c
static void init_sdl()
{
#ifdef PLATFORM_POSIX
    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
#endif
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
        log_fatal("Could not initialize SDL\n%s", SDL_GetError());

    SDL_GetDesktopDisplayMode(0, &display_mode);
    if (display_mode.refresh_rate <= 0) {
        log_debug("Display reported refresh rate %i, assuming 60 Hz", display_mode.refresh_rate);
        display_mode.refresh_rate = 60;
    }
    geo.screen_width = display_mode.w;
    geo.screen_height = display_mode.h;
    refresh_period = 1000 / (Uint32) display_mode.refresh_rate;
    geo.screen_margin = (int) (SCREEN_MARGIN * (float) geo.screen_height);
}
```

Replace `create_window()` with the two functions below. The bodies are the current code, split at the renderer, with the blend-mode call moved after the NULL check:

```c
// A function to create the window (once per process)
static void create_window()
{
    window = SDL_CreateWindow(PROJECT_NAME,
                 SDL_WINDOWPOS_UNDEFINED,
                 SDL_WINDOWPOS_UNDEFINED,
                 0,
                 0,
                 SDL_WINDOW_FULLSCREEN_DESKTOP
             );
    if (window == NULL)
        log_fatal("Could not create SDL Window\n%s", SDL_GetError());
    SDL_ShowCursor(SDL_DISABLE);
#ifdef _WIN32
    SDL_VERSION(&wm_info.version);
    SDL_GetWindowWMInfo(window, &wm_info);
#endif
}

// A function to create the renderer (on every config load; VSync is a renderer flag)
static int create_renderer()
{
    Uint32 renderer_flags = SDL_RENDERER_ACCELERATED;
    if (!config.vsync) {
        if (config.fps_limit > MIN_FPS_LIMIT && config.fps_limit <= display_mode.refresh_rate)
            refresh_period = 1000 / (Uint32) config.fps_limit;
        else
            config.vsync = true;
    }
    if (config.vsync) {
        refresh_period = 1000 / (Uint32) display_mode.refresh_rate;
        renderer_flags |= SDL_RENDERER_PRESENTVSYNC;
    }
    if (config.gamepad_enabled) {
        delay_period = GAMEPAD_REPEAT_DELAY / refresh_period;
        repeat_period = GAMEPAD_REPEAT_INTERVAL / refresh_period;
        if (!repeat_period)
            repeat_period = 1;
    }
    if (slideshow != NULL)
        slideshow->transition_change_rate = 255.0f / ((float) config.slideshow_transition_time / (float) refresh_period);

    renderer = SDL_CreateRenderer(window, -1, renderer_flags);
    if (renderer == NULL) {
        log_error("Could not initialize renderer\n%s", SDL_GetError());
        return 1;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    set_draw_color();
#ifdef _WIN32
    if (config.background_mode == BACKGROUND_TRANSPARENT)
        make_window_transparent();
#endif
    return 0;
}
```

Replace `init_sdl_ttf()` with `load_title_font()`. `TTF_Init()` moves to `main()`.

```c
// A function to load the title font from the config
static int load_title_font()
{
    title_info = (TextInfo) {
        .font_size = (int) config.title_font_size,
        .shadow = config.title_shadows,
        .font_path = &config.title_font_path,
        .max_width = config.icon_size,
        .oversize_mode = config.title_oversize_mode,
        .color = &config.title_font_color
    };
    if (config.title_shadows) {
        title_info.shadow_color = &config.title_shadow_color;
        calculate_shadow_alpha(title_info);
    }
    else
        title_info.shadow_color = NULL;

    if (load_font(&title_info, FILENAME_DEFAULT_FONT)) {
        log_error("Could not load title font");
        return 1;
    }
    geo.font_height = config.titles_enabled ? TTF_FontHeight(title_info.font) : 0;
    return 0;
}
```

In `init_slideshow()`, add `slideshow = NULL;` right after **both** existing `quit_slideshow();` calls in the error branches. Otherwise the freed pointer stays non-NULL and unload would free it twice.

- [ ] **Step 3: `launcher_unload`, `cleanup`, `quit`**

Replace `cleanup()` with:

```c
// A function to free the menu, hotkey, and gamepad control lists built by the parser
static void free_config_lists()
{
    Menu *next_menu;
    for (Menu *m = config.first_menu; m != NULL; m = next_menu) {
        next_menu = m->next;
        Entry *next_entry;
        for (Entry *e = m->first_entry; e != NULL; e = next_entry) {
            next_entry = e->next;
            free(e->title);
            free(e->icon_path);
            free(e->icon_selected_path);
            free(e->cmd);
            free(e);
        }
        free(m->name);
        free(m);
    }
    config.first_menu = NULL;
    config.num_menus = 0;

    Hotkey *next_hotkey;
    for (Hotkey *h = hotkeys; h != NULL; h = next_hotkey) {
        next_hotkey = h->next;
        free(h->cmd);
        free(h);
    }
    hotkeys = NULL;

    GamepadControl *next_control;
    for (GamepadControl *g = gamepad_controls; g != NULL; g = next_control) {
        next_control = g->next;
        free(g->cmd);
        free(g);
    }
    gamepad_controls = NULL;
}

// A function to free everything launcher_load() created, keeping the window and SDL itself
static void launcher_unload()
{
    // Worker threads render into clock/slideshow buffers, so stop them first
    SDL_WaitThread(Slideshowhread, NULL);
    Slideshowhread = NULL;
    SDL_WaitThread(clock_thread, NULL);
    clock_thread = NULL;

#ifdef _WIN32
    unregister_exit_hotkey();
#endif
    if (gamepads != NULL)
        disconnect_gamepad(-1, true, true);

    if (title_info.font != NULL) {
        TTF_CloseFont(title_info.font);
        title_info.font = NULL;
    }
    if (clk != NULL) {
        if (clk->text_info.font != NULL)
            TTF_CloseFont(clk->text_info.font);
        SDL_FreeSurface(clk->time_surface);
        SDL_FreeSurface(clk->date_surface);
        free(clk);
        clk = NULL;
    }
    if (slideshow != NULL) {
        SDL_FreeSurface(slideshow->transition_surface);
        quit_slideshow();
        slideshow = NULL;
    }

    // Destroying the renderer frees every texture created with it
    if (renderer != NULL) {
        SDL_DestroyRenderer(renderer);
        renderer = NULL;
    }
    background_texture = NULL;
    background_overlay = NULL;
    free(highlight);
    highlight = NULL;
    free(scroll);
    scroll = NULL;
    free(screensaver);
    screensaver = NULL;

    free_config_lists();
    default_menu = NULL;
    current_menu = NULL;
    current_entry = NULL;

    free(config.default_menu);
    free(config.background_image);
    free(config.title_font_path);
    free(config.slideshow_directory);
    free(config.clock_font_path);
    free(config.gamepad_mappings_file);
    free(config.startup_cmd);
    free(config.quit_cmd);
    config.default_menu = NULL;
    config.background_image = NULL;
    config.title_font_path = NULL;
    config.slideshow_directory = NULL;
    config.clock_font_path = NULL;
    config.gamepad_mappings_file = NULL;
    config.startup_cmd = NULL;
    config.quit_cmd = NULL;

    bool has_focus = state.has_focus;
    state = (State) { .has_focus = has_focus };
}

// A function to close subsystems and free memory before quitting
static void cleanup()
{
    launcher_unload();
    if (window != NULL) {
        SDL_DestroyWindow(window);
        window = NULL;
    }
    quit_svg();
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
    free(config.exe_path);
    free(config.config_path);
    if (log_file != NULL)
        fclose(log_file);
}
```

In `quit()`, change the `quit_cmd` block to the following, because `launcher_unload()` now frees it:

```c
    if (config.quit_cmd != NULL)
        execute_command(config.quit_cmd);
```

- [ ] **Step 4: `launcher_load`, `launcher_reload`, `restore_config_backup`**

Add `#include "ini_doc.h"` and `#include "platform/fileops.h"` to the `src/launcher.c` includes. Then add, above `main()`:

```c
// A function to load the config file and build everything that depends on it
static int launcher_load()
{
    // Start from built-in defaults, keeping what came from the command line
    char *exe_path = config.exe_path;
    char *config_path = config.config_path;
    bool debug = config.debug;
    memcpy(&config, &default_config, sizeof(Config));
    config.exe_path = exe_path;
    config.config_path = config_path;
    config.debug = debug;

    if (parse_config_file(config.config_path))
        return 1;
    if (config.default_menu == NULL) {
        log_error("No default menu defined in config file");
        return 1;
    }
    default_menu = get_menu(config.default_menu);
    if (default_menu == NULL) {
        log_error("Default menu %s not found in config file", config.default_menu);
        return 1;
    }

    if (config.inhibit_os_screensaver)
        SDL_DisableScreenSaver();
    else
        SDL_EnableScreenSaver();

    if (load_title_font())
        return 1;
    validate_settings(&geo);
    if (config.background_mode == BACKGROUND_SLIDESHOW)
        init_slideshow();
    if (create_renderer())
        return 1;
    ticks.main = SDL_GetTicks();
    ticks.last_input = ticks.main;

    // Gamepad subsystem follows the setting. SDL only announces already-attached
    // devices when the subsystem starts, so on reload connect them here.
    if (config.gamepad_enabled) {
        bool already_running = SDL_WasInit(SDL_INIT_GAMECONTROLLER) != 0;
        if (!already_running && SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) < 0)
            log_error("Could not initialize gamepad subsystem\n%s", SDL_GetError());
        if (config.gamepad_mappings_file != NULL &&
        SDL_GameControllerAddMappingsFromFile(config.gamepad_mappings_file) < 0)
            log_error("Could not load gamepad mappings from %s\n%s", config.gamepad_mappings_file, SDL_GetError());
        if (already_running) {
            for (int i = 0; i < SDL_NumJoysticks(); i++) {
                if (SDL_IsGameController(i) && (config.gamepad_device < 0 || config.gamepad_device == i))
                    connect_gamepad(i, true, false);
            }
        }
    }
    else if (SDL_WasInit(SDL_INIT_GAMECONTROLLER))
        SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
```

Then paste, **unchanged**, the blocks from the current `main()` that start at `// Render background` and run through `// Render background overlay` and its block. These are background image/slideshow, `init_screensaver`, clock, highlight, scroll indicators, and overlay. Make **one** change in the clock block: `clk = malloc(sizeof(Clock));` becomes `clk = calloc(1, sizeof(Clock));`, so `launcher_unload` can safely free surfaces that were never set. Then finish the function:

```c
#ifdef _WIN32
    if (has_exit_hotkey())
        register_exit_hotkey();
#endif

    if (config.debug) {
        debug_video(renderer, &display_mode);
        debug_settings();
        debug_gamepad(gamepad_controls);
        debug_hotkeys(hotkeys);
        debug_menu_entries(config.first_menu, config.num_menus);
    }

    if (load_menu(default_menu, false, true)) {
        log_error("Could not load default menu %s", config.default_menu);
        return 1;
    }
    return 0;
}

// A function to put config.ini.bak back in place, keeping the bad file as config.ini.failed
static int restore_config_backup(const char *path)
{
    char backup_path[MAX_PATH_CHARS + 1];
    char failed_path[MAX_PATH_CHARS + 1];
    snprintf(backup_path, sizeof(backup_path), "%s.bak", path);
    snprintf(failed_path, sizeof(failed_path), "%s.failed", path);
    IniDoc *backup = ini_doc_load(backup_path);
    if (backup == NULL)
        return 1;
    remove(failed_path);
    replace_file(path, failed_path);
    int error = ini_doc_save(backup, path);
    ini_doc_free(backup);
    return error;
}

// A function to re-read config.ini and rebuild the launcher without closing the window
int launcher_reload()
{
    log_debug("Reloading configuration from %s", config.config_path);
    launcher_unload();
    int error = launcher_load();
    if (error) {
        log_error("Reload failed, restoring the previous configuration");
        launcher_unload();
        if (restore_config_backup(config.config_path) || launcher_load())
            log_fatal("Could not load %s or its backup %s.bak", config.config_path, config.config_path);
    }
    return error;
}
```

- [ ] **Step 5: New `main()` start and main-loop reload**

Replace everything in `main()` from its first line through the `// Execute startup command` block with:

```c
int main(int argc, char *argv[])
{
    memcpy(&config, &default_config, sizeof(Config));
    config.exe_path = SDL_GetBasePath();
    handle_arguments(argc, argv, &config.config_path);

    // One-time setup: SDL, image/font libraries, SVG rasterizer, window
    init_sdl();
    init_sdl_image();
    if (TTF_Init() == -1)
        log_fatal("Could not initialize SDL_ttf\n%s", TTF_GetError());
    init_svg();
    create_window();
    ticks.program_start = SDL_GetTicks();

    if (launcher_load())
        log_fatal("Could not load configuration from %s", config.config_path);

    // Execute startup command
    if (config.startup_cmd != NULL)
        execute_command(config.startup_cmd);
```

Leave the rest of `main()` as it is (the `// Main program loop` onward), except insert at the top of the `while (1)` loop, right after `ticks.main = SDL_GetTicks();`:

```c
        // Apply a :reload requested during the previous frame
        if (state.reload_pending) {
            state.reload_pending = false;
            launcher_reload();
            SDL_FlushEvents(SDL_KEYDOWN, SDL_MOUSEWHEEL);
            ticks.main = SDL_GetTicks();
        }
```

Delete the now-unused `int error;` and `char *config_file_path = NULL;` locals, plus any leftover code in `main()` that the moved blocks duplicated. After this step, `main()` contains only the startup above and the main loop.

- [ ] **Step 6: Handle `:reload`**

In `execute_command()`, add after the `SCMD_SLEEP` branch:

```c
        else if (!strcmp(special_command, SCMD_RELOAD))
            state.reload_pending = true;
```

- [ ] **Step 7: Build with warnings**

Run: `cmake -B build -DCMAKE_BUILD_TYPE=Debug -DEXTRA_WARNINGS=ON && cmake --build build 2>&1 | grep -E "error|warning: .*(launcher|util|config_io|ini_doc|settings_schema)\.c" | grep -v "no previous prototype\|without a prototype\|GNU extension"`
Expected: no errors and no new warnings in the touched files. Warnings that already exist upstream, such as `()` prototypes and the `##__VA_ARGS__` GNU extension, are fine.

Run: `ctest --test-dir build-tests --output-on-failure` (after `cmake --build build-tests`)
Expected: all suites still PASS.

- [ ] **Step 8: Manual verification (macOS here; same steps on Linux)**

Create `.context/reload-test.ini`. Copy `build/config.ini` to that path, then edit the copy:
- under `[Hotkeys]`, add `Hotkey2=#72;:reload` (the R key)
- set `[Clock] Enabled=true`
- set `[Background] Mode=Slideshow` and `SlideshowDirectory=` to a folder with 3+ images, e.g. `docs/assets/screenshots` plus two copies

Run `./build/flex-launcher -d -c .context/reload-test.ini` and check each item. Use `tail -f ~/.local/share/flex-launcher/flex-launcher.log` alongside.

1. **Live edit:** in another terminal, change `IconSize=256` to `IconSize=128` and `Color=` under `[Titles]` to `#FF0000`, then press **R**. Expected: smaller icons and red titles, the window stays open (no flash to desktop), and the log shows `Reloading configuration`.
2. **VSync toggle:** set `VSync=false` and `FPSLimit=30`, press R. Expected: still renders, and the log's video debug shows the new mode.
3. **Default menu:** add `[Second]` with one entry, set `DefaultMenu=Second`, press R. Expected: shows `Second`.
4. **Broken config falls back:** first make a backup by copying the file to `reload-test.ini.bak`. Then set `DefaultMenu=DoesNotExist` and press R. Expected:
   - the log shows `Default menu DoesNotExist not found` and `restoring the previous configuration`
   - the launcher keeps running on the `.bak` contents
   - `.context/reload-test.ini.failed` exists
5. **Threads:** while a slideshow crossfade is running and the clock is visible, hold **R** for about 5 seconds (key repeat triggers dozens of reloads). Expected: no crash, and the slideshow and clock keep working afterwards.
6. **Memory:** note `ps -o rss= -p $(pgrep -x flex-launcher)`, hold R for 10 seconds, wait 5 seconds, then measure again. Expected: RSS within ±5 MB. On macOS also run `leaks $(pgrep -x flex-launcher) | grep "leaks for"`. Expected: no leaks from `launcher_load`, `config_handler`, or `add_hotkey`. Leaks inside system frameworks can be ignored.
7. **Gamepad (Review Focus 5):** with a controller connected, set `[Gamepad] Enabled=true`, start, confirm the D-pad moves. Press R and confirm the D-pad still moves. Then set `Enabled=false` and press R: the D-pad no longer works. Set it back to `true`, press R: it works again. If no controller is available, write "gamepad reload: NOT VERIFIED (no controller)" in the commit message.
8. **Esc** still quits after a reload, and `QuitCmd` (if set) runs once.

Write down the result of each item for the commit message.

- [ ] **Step 9: Commit**

```bash
git add src/launcher.c src/launcher.h src/image.c src/platform/win32.c src/platform/platform.h
git commit -m "Reload config in place: launcher_load/unload and :reload command

Manual checks: <list results of items 1-8>"
```

---

### Task 10: Docs, cross-platform build check, PR

**Files:**
- Modify: `docs/configuration.md`, `CHANGELOG`, `CLAUDE.md`, `docs/superpowers/specs/2026-10-16-config-foundation-design.md` (status line only)

- [ ] **Step 1: Document `:reload` and the tests**

In `docs/configuration.md`, find the special-commands list (search for `:sleep`) and add, in the same format as its neighbors:

```markdown
##### :reload
Re-reads the config file and applies it without restarting. If the new file can't be loaded, the launcher restores `config.ini.bak` (written by the in-app settings editor), keeps the broken file as `config.ini.failed`, and logs the error.
```

In `CHANGELOG`, under `Unreleased`, add:

```
- Add :reload special command to apply config.ini changes without restarting
- Fix leaks and stale parser state when the config is re-read
```

In `CLAUDE.md`'s Build section, add:

```markdown
Unit tests (config file editing, settings schema, save checks):
```bash
cmake -B build-tests -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build-tests --target reflex-tests && ctest --test-dir build-tests --output-on-failure
```
```

Also update the line "There are no unit tests." to say that unit tests cover `ini_doc`, `settings_schema`, and `config_io`, while UI and reload behavior are verified manually.

In `CLAUDE.md`'s "Config settings pipeline" list, add a step: **add the setting to `settings_schema[]` in `src/settings_schema.c`** (the `schema` test fails if you forget).

Set the spec's `Status:` line to `Implemented (sub-project 1)`.

- [ ] **Step 2: Full local verification**

Run:
```bash
cmake -B build-tests -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DEXTRA_WARNINGS=ON && cmake --build build-tests && ctest --test-dir build-tests --output-on-failure
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build && ./build/flex-launcher -v
```
Expected: 3/3 test suites pass, and the release build prints the version.

- [ ] **Step 3: Commit, push, cross-platform build**

```bash
git add docs/ CHANGELOG CLAUDE.md
git commit -m "Document :reload and unit tests"
git push -u origin HEAD
gh workflow run Build --repo davetait/reflex-launcher --ref "$(git branch --show-current)"
```

Watch it with `gh run watch --repo davetait/reflex-launcher $(gh run list --repo davetait/reflex-launcher --branch "$(git branch --show-current)" --limit 1 --json databaseId --jq '.[0].databaseId')`.
Expected: the Windows, Linux, Raspberry Pi, and macOS jobs all succeed. If Windows fails, fix it before opening the PR. Typical causes are `MoveFileExA`/`UnregisterHotKey` includes and `snprintf` of `%zu` on old MSVC.

- [ ] **Step 4: Open the PR**

```bash
gh pr create --repo davetait/reflex-launcher --base main --title "Config foundation: ini_doc, settings schema, safe save, :reload" --body "<summary of tasks 1-9, test results, manual check results, Windows reload NOT VERIFIED (build-only)>"
```

The PR body must include the manual verification results from Task 9 Step 8. It must also mark Windows reload behavior **NOT VERIFIED** (build-checked only).
