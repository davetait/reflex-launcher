# CLAUDE.md

This file guides Claude Code (claude.ai/code) when working in this repository.

## Project
Flex Launcher is a C11/SDL2 "10-foot UI" application launcher for HTPCs (Windows, Linux, Raspberry Pi). Upstream says it is in maintenance mode: bug fixes and dependency updates only.

## Build
Dependencies: SDL2 ≥ 2.0.14, SDL2_image ≥ 2.0.5, SDL2_ttf ≥ 2.0.15, inih (plus getopt on Windows via vcpkg). Linux finds them through pkg-config. Windows uses vcpkg (`vcpkg.json`, triplet `x64-windows-static`).

macOS is not supported and does not compile as-is. Apple clang doesn't define `__unix__`, so every `#ifdef __unix__` takes the Windows-style `#else` branch, and `src/image.c` then references `PATH_FONTS_RELATIVE`, which is defined only under `_WIN32`. Shutdown, restart and sleep also call `systemctl`.

Linux:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug     # add -DRPI=1 for Pi defaults, -DEXTRA_WARNINGS=ON for -Wall -Wextra -Wpedantic -Wconversion
cmake --build build
./build/flex-launcher -d                    # -d writes debug log to ~/.local/share/flex-launcher/flex-launcher.log
./build/flex-launcher -c path/to/config.ini
```

Packaging (matches CI): `-DCMAKE_INSTALL_PREFIX=/usr -DPACKAGE=DEB` then `cmake --build build --target package`. `-DPACKAGE=ARCH` only generates a PKGBUILD and stops.

There are no unit tests. CI (`.github/workflows/build.yml`) only checks that Windows, Debian bullseye, and RPi (qemu chroot) builds succeed. To verify a change, build with `EXTRA_WARNINGS=ON` and run with `-d`, using the generated `build/config.ini`.

## Architecture
- `src/launcher.c`: `main()`, SDL init, window creation, the main loop (`SDL_PollEvent` → state updates → `draw_screen()`), input handling, menu navigation, gamepad hot-plug, and `execute_command()` for special commands (`:submenu`, `:back`, `:shutdown`, …; see `SCMD_*` in `launcher.h`). Process-wide state lives in globals defined here (`config`, `state`, `ticks`, `geo`, `renderer`, …). Other files access them with `extern`.
- `src/launcher.h`: all shared types (`Config`, `State`, `Ticks`, `Menu`/`Entry`/`Gamepad`/`Hotkey` linked lists, `Geometry`) and tunable limits (`MIN_*`/`MAX_*`).
- `src/util.c`: INI parsing (`config_handler`, called by inih), `validate_settings()` for range clamping and geometry, CLI args, path and UTF-8 helpers.
- `src/image.c`: texture loading, SVG rasterizing via nanosvg (highlight and scroll indicators are generated as SVG strings, see `image.h`), text rendering, slideshow image loading.
- `src/clock.c`: clock/date rendering.
- `src/debug.c`: logging (`log_debug`/`log_error`/`log_fatal`) and `-d` diagnostics dumps.
- `src/platform/`: OS layer behind `platform.h` (process launch, shutdown/sleep, file checks, slideshow dir scan). `unix.c` or `win32.c` is compiled into a static `platform` lib. Shared files guard Windows-only code with `#ifdef _WIN32` and Linux-only code with `#ifdef __unix__`. The two are not exhaustive: other platforms fall into the `#else` branch of `__unix__` checks.
- `src/external/`: vendored nanosvg. Do not edit; update from upstream instead.

Threads: the slideshow and clock render surfaces on `SDL_CreateThread` workers and signal through `State` flags (`slideshow_background_ready`, `clock_ready`). Only the main thread may touch the `SDL_Renderer`/textures.

Application launch detection relies on window focus, not process status: losing focus while `application_launching` means the app started, and regaining focus means it exited. `ApplicationTimeout` resets this if focus never changes.

Frame pacing: with `VSync=false`, `draw_screen()` sleeps for `refresh_period - elapsed`, using unsigned `Uint32` tick math. Watch for underflow when changing timing code.

## Config settings pipeline (generated headers)
Config keys are not hard-coded in C. To add or rename a setting:
1. `config/config_settings.cmake`: `set(SETTING_FOO "Foo")`
2. `config/launcher_config.h.in`: `#define SETTING_FOO "@SETTING_FOO@"` (CMake generates `build/launcher_config.h`)
3. `src/launcher.h`: add a field to `Config`. `src/launcher.c`: set its default in the `Config config = {…}` initializer.
4. `src/util.c`: parse it in `config_handler` under the right `[Section]`, and clamp it in `validate_settings()` if needed.
5. `config/config.ini.in`: the template for both `build/config.ini` and the installed config. Default app commands and icons differ per platform/RPi (top-level `CMakeLists.txt`).
6. Document it in `docs/configuration.md` (Jekyll site under `docs/`) and note it in `CHANGELOG`.

## Conventions
- 4-space indent, `snake_case` functions, `PascalCase` typedefs, `static` for file-local functions with prototypes at the top of the file.
- Use `log_error` for recoverable problems and `log_fatal` (exits) for unrecoverable ones. Prefer falling back to a safe default over aborting (see background image → color fallback in `main()`).
- Keep code portable across MSVC and GCC. Avoid GNU-only extensions in shared files.
