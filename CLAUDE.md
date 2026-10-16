# CLAUDE.md

This file guides Claude Code (claude.ai/code) when working in this repository.

## Project
Flex Launcher is a C11/SDL2 "10-foot UI" application launcher for HTPCs (Windows, Linux, Raspberry Pi). This fork also builds on macOS. Upstream says it is in maintenance mode: bug fixes and dependency updates only.

## Build
Dependencies: SDL2 ≥ 2.0.14, SDL2_image ≥ 2.0.5, SDL2_ttf ≥ 2.0.15, inih (plus getopt on Windows via vcpkg). Linux and macOS find them through pkg-config (macOS: `brew install cmake pkgconf sdl2 sdl2_image sdl2_ttf inih`). Windows uses vcpkg (`vcpkg.json`, triplet `x64-windows-static`).

Linux / macOS:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug     # add -DRPI=1 for Pi defaults, -DEXTRA_WARNINGS=ON for -Wall -Wextra -Wpedantic -Wconversion
cmake --build build
./build/flex-launcher -d                    # -d writes debug log to ~/.local/share/flex-launcher/flex-launcher.log
./build/flex-launcher -c path/to/config.ini
```

Packaging (matches CI): `-DCMAKE_INSTALL_PREFIX=/usr -DPACKAGE=DEB` then `cmake --build build --target package`. `-DPACKAGE=ARCH` only generates a PKGBUILD and stops.

Unit tests (config file editing, settings schema, save checks):
```bash
cmake -B build-tests -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build-tests --target reflex-tests && ctest --test-dir build-tests --output-on-failure
```

Unit tests cover `ini_doc`, `settings_schema`, `config_io`, `settings_model`, `text_input`, `file_browser`, and `ui_controller`, while UI and reload behavior are verified manually. CI (`.github/workflows/build.yml`) does not run on pushes or PRs. It runs only on `v*` tags (building Windows, Debian bullseye, macOS, and RPi packages and uploading all but macOS to the GitHub Release) or manually via `gh workflow run Build --ref <branch>`, which builds without releasing. To verify a change, build with `EXTRA_WARNINGS=ON` and run with `-d`, using the generated `build/config.ini`.

## Architecture
- `src/launcher.c`: `main()`, SDL init, window creation, the main loop (`SDL_PollEvent` → state updates → `draw_screen()`), input handling, menu navigation, gamepad hot-plug, and `execute_command()` for special commands (`:submenu`, `:back`, `:shutdown`, …; see `SCMD_*` in `launcher.h`). Process-wide state lives in globals defined here (`config`, `state`, `ticks`, `geo`, `renderer`, …). Other files access them with `extern`.
- `src/launcher.h`: all shared types (`Config`, `State`, `Ticks`, `Menu`/`Entry`/`Gamepad`/`Hotkey` linked lists, `Geometry`) and tunable limits (`MIN_*`/`MAX_*`).
- `src/util.c`: INI parsing (`config_handler`, called by inih), `validate_settings()` for range clamping and geometry, CLI args, path and UTF-8 helpers.
- `src/image.c`: texture loading, SVG rasterizing via nanosvg (highlight and scroll indicators are generated as SVG strings, see `image.h`), text rendering, slideshow image loading.
- `src/clock.c`: clock/date rendering.
- `src/debug.c`: logging (`log_debug`/`log_error`/`log_fatal`) and `-d` diagnostics dumps.
- `src/platform/`: OS layer behind `platform.h` (process launch, shutdown/sleep, file checks, slideshow dir scan). `unix.c` or `win32.c` is compiled into a static `platform` lib. Shared files guard Windows-only code with `#ifdef _WIN32` and Linux/macOS code with `#ifdef PLATFORM_POSIX` (defined by CMake when `UNIX`). Never use `__unix__`: Apple clang doesn't define it. Mac-only differences (e.g. `osascript`/`pmset` power commands in `unix.h`) use `#ifdef __APPLE__`.
- `src/config_defaults.c`: `default_config`, the built-in defaults copied into `config` at startup and before every reload.
- `src/ini_doc.*`: line-preserving INI document for editing `config.ini`. Mirrors inih's parsing rules (comments, inline ` ;`, continuations, BOM, duplicate sections) so `ini_doc_get` returns what the launcher loads; edits keep comments and refuse values, keys, or section names inih would read back differently. `ini_doc_save` writes `.tmp`, copies the old file to `.bak`, then replaces it.
- `src/settings_schema.*`: table of every scalar setting (section, key, type, range, label, help) and `schema_validate()`. The `schema` test fails if a `SETTING_*` key is missing.
- `src/config_io.*`: `config_check()` (default menu, entries, submenus, hotkeys) and `config_save()` (validate changed values and line length, detect changes on disk, save).
- `src/config_ui/`: the in-app Settings screen, opened by the `:config` special command. `settings_model`, `text_input`, `file_browser` and `ui_controller` are display-free and unit-tested. `ui_render` and `ui_text` draw, and `config_ui` is the SDL glue (input mapping, loop, text input). `reflex-ui-snapshot` renders UI states to PNGs: build the `reflex-ui-snapshot` target in the tests build, then run `build-tests/reflex-ui-snapshot <config.ini> <outdir>`. The UI never calls `launcher_reload`; after saving it closes and calls `launcher_request_reload()`.
- `src/platform/fileops.h`: `replace_file()`, an atomic rename-over implemented in `unix.c`/`win32.c`.
- Reload: `launcher_load()`/`launcher_unload()` in `launcher.c` build and free everything config-dependent (keeping the window and SDL). `launcher_reload()` is static and runs only at the top of the main loop; anything else (e.g. the `:reload` command) calls `launcher_request_reload()`, because a reload frees the menus, entries, and hotkeys the caller may be iterating.
- `src/external/`: vendored nanosvg. Do not edit; update from upstream instead.

Threads: the slideshow and clock render surfaces on `SDL_CreateThread` workers and signal through `State` flags (`slideshow_background_ready`, `clock_ready`). Only the main thread may touch the `SDL_Renderer`/textures.

Application launch detection relies on window focus, not process status: losing focus while `application_launching` means the app started, and regaining focus means it exited. `ApplicationTimeout` resets this if focus never changes.

Frame pacing: with `VSync=false`, `draw_screen()` sleeps for `refresh_period - elapsed`, using unsigned `Uint32` tick math. Watch for underflow when changing timing code.

## Config settings pipeline (generated headers)
Config keys are not hard-coded in C. To add or rename a setting:
1. `config/config_settings.cmake`: `set(SETTING_FOO "Foo")`
2. `config/launcher_config.h.in`: `#define SETTING_FOO "@SETTING_FOO@"` (CMake generates `build/launcher_config.h`)
3. `src/launcher.h`: add a field to `Config`. `src/config_defaults.c`: set its default in the `const Config default_config = {…}` initializer.
4. `src/util.c`: parse it in `config_handler` under the right `[Section]`, and clamp it in `validate_settings()` if needed.
5. `src/settings_schema.c`: add the setting to `settings_schema[]` (the `schema` test fails if you forget).
6. `config/config.ini.in`: the template for both `build/config.ini` and the installed config. Default app commands and icons differ per platform/RPi (top-level `CMakeLists.txt`).
7. Document it in `docs/configuration.md` (Jekyll site under `docs/`) and note it in `CHANGELOG`.

## Conventions
- 4-space indent, `snake_case` functions, `PascalCase` typedefs, `static` for file-local functions with prototypes at the top of the file.
- Use `log_error` for recoverable problems and `log_fatal` (exits) for unrecoverable ones. Prefer falling back to a safe default over aborting (see background image → color fallback in `main()`).
- Keep code portable across MSVC and GCC. Avoid GNU-only extensions in shared files.
