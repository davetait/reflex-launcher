# Settings Screen (Config GUI sub-project 2 of 4)

Date: 2026-10-16
Status: Draft. The user said "go with sensible defaults", so every choice below is the builder's call unless it is marked as the user's.

## Intent

This is the user's own requirement, from the original request: a **Config** entry that opens an in-app screen covering every `config.ini` option, with the best control for each kind of option. Examples are toggles for on/off, dropdowns for limited options, and text input for free-form text. It must be fully operable from the couch with a remote or gamepad, and a physical keyboard is optional. Changes are staged. Save writes `config.ini` (keeping `.bak`) and reloads the launcher in place. Cancel discards. This was agreed in the sub-project 1 brainstorm.

This sub-project covers every **scalar** setting (the 93 rows in `settings_schema[]`). Menus and hotkeys (lists) are sub-project 3. App and website picking is sub-project 4.

## Foundation it builds on (PR #7)

The foundation from PR #7 provides:

- `ini_doc`: edits `config.ini` while preserving comments. Every edit re-parses the document, so **pointers from `ini_doc_get`/`get_list`/`sections` are invalidated by any edit**. Copy values before editing.
- `settings_schema`: type, range, choices, label, help and sample default for each setting, plus `schema_validate()`.
- `config_io`: `config_check()` and `config_save()`.
- `launcher_request_reload()`.

## Decisions (builder's calls)

### Entry point

- A new special command `:config` opens the screen.
- The shipped sample `config.ini` gets a **Settings** entry at the end of the default menu. The icon is a new `assets/icons/settings.svg`, 256×256, styled like the existing icons. SDL_image loads SVG.
- Existing installs add `EntryN=Settings;<icons>/settings.svg;:config` themselves. This is documented.

### Screen

- **Layout:** a full-screen dark panel (about 92% opaque) over the launcher background.
  - **Header:** "Settings".
  - **Left column:** the 9 sections (General, Background, Layout, Titles, Highlight, Scroll Indicators, Clock, Screensaver, Gamepad).
  - **Right area:** the selected section's settings as rows, with the label on the left and the control on the right.
  - **Footer:** the focused row's help text and button hints.
- **Scrolling:** the row list scrolls to keep the focused row visible.
- **Font and colors:** the UI uses its own font (the bundled default font) at sizes derived from the screen height, plus a fixed theme. A broken font or color setting can't make the settings screen unreadable.

### Controls by setting type

| Type | Control | Left/Right | Select |
|---|---|---|---|
| On/off (`SET_BOOL`) | toggle switch | flip | flip |
| Fixed choice (`SET_CHOICE`) and menu name (`SET_MENU_REF`) | `‹ value ›` selector | previous/next (wraps) | opens a list to pick from |
| Whole number (`SET_INT`) | `‹ 256 ›` stepper | step down/up (step size scales with the range), clamped | opens the on-screen keyboard (numeric) |
| Percentage (`SET_PERCENT`) | slider bar + "50%" | ±5%, clamped to range | opens the on-screen keyboard |
| % or pixels (`SET_LENGTH`) | shows "5%" or "40 px" | ±5 in the current unit | opens the on-screen keyboard (type "40" or "5%") |
| Decimal (`SET_DECIMAL`) | `‹ 1.5 ›` stepper | ±0.5 | opens the on-screen keyboard |
| Color (`SET_COLOR`) | swatch + hex | — | color picker: 16 presets + "Custom hex…" (keyboard) |
| Text / command (`SET_TEXT`, `SET_COMMAND`) | value text | — | on-screen keyboard |
| File / folder / font (`SET_FILE`, `SET_DIR`, `SET_FONT`) | value text (path shown from its end) | — | file browser, filtered by type: images for files, `.ttf`/`.otf` for fonts, folders only for directories |

- **Unset values** show their default dimmed, e.g. "Default: 256". Settings with no default show "Not set".
- **Reset:** turns a row back to unset, so the launcher's built-in default applies. Use the Y button or the Delete key.
- **Invalid entries:** anything `schema_validate` rejects isn't applied. A short message appears in the footer.

### Input (fixed mapping, independent of the user's bindings, so a bad binding can't lock them out)

| Action | Keyboard | Gamepad |
|---|---|---|
| Move | arrows | D-pad, left stick (with repeat) |
| Select / OK | Enter, keypad Enter | A |
| Back / Cancel | Escape, Backspace (outside text entry) | B |
| Reset to default | Delete | Y |
| Save | Ctrl+S, or F2 | Start |
| Text entry | physical typing (SDL text input) plus the on-screen keyboard | on-screen keyboard |

While the screen is open, the launcher's own hotkeys, gamepad bindings and screensaver are suspended. `Esc → :quit` can't quit from inside Settings.

### Navigation and saving

- **Focus:** it starts on the section list. Right or Select enters the rows, and Left or Back returns to the sections.
- **Back from the section list** closes the screen:
  - With no changes, it closes immediately.
  - With unsaved changes, a dialog offers **Save**, **Discard** and **Keep editing**.
- **Save works from anywhere** (Start, Ctrl+S, F2, or the dialog). It runs `config_save()`.
  - **On success:** the screen closes and `launcher_request_reload()` applies the changes.
  - **On failure:** a message dialog shows the error and the screen stays open. If `config.ini` was edited outside the app, the dialog offers **Overwrite** or **Cancel**.
- **Unopenable config:** if `config.ini` can't be opened for editing (missing or unreadable), a message says so and the screen closes when it is dismissed.

### On-screen keyboard

- **Pages:** letters (with Shift), numbers and symbols, and a numeric-only page for numeric fields.
- **Keys:** Space, Backspace, ← →, Done, Cancel. The text field shows a cursor.
- **Physical keyboard:** typing, Backspace, arrows and Enter work while the keyboard is open.

### File browser

- **Start folder:** the current value's folder if it exists, otherwise the config file's folder, otherwise home.
- **List:** `..` first, then folders, then matching files, sorted case-insensitively. Hidden entries (dot-files) are skipped.
- **Picking:** for folder settings, a "Use this folder" item at the top.

## Testability

The pieces are split so that most behavior is unit-tested without a display:

- **Model:** values, stepping, set and reset, save.
- **Controller:** actions → state changes, across all modes.
- **Keyboard and file-browser models.**

Rendering is checked visually through a snapshot tool. It draws each screen state into PNG files with SDL's software renderer, with no window or keyboard needed.

## Out of scope

- Menus and hotkeys editing (sub-project 3).
- App and website picker (sub-project 4).
- Live preview of changes.
- Mouse or touch input.
- Localization.
- Automatically adding the Settings entry to existing user configs.
