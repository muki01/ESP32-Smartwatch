# Contributing to ESP32 Smartwatch

Thanks for your interest in the project! Bug reports, ideas, documentation fixes and pull requests are all welcome.

## Reporting bugs

Open an [issue](https://github.com/muki01/ESP32-Smartwatch/issues/new/choose) and include:

- the firmware version (Settings › About) and your board,
- the Arduino esp32 core and LVGL versions,
- what you did, what you expected and what happened,
- the serial log (115200 baud). Crash dumps (`Backtrace: ...`) are especially helpful.

## Suggesting features

Open a feature request and describe the use case: what you want to do on the watch and why. Screenshots or sketches of the screen are welcome.

## Pull requests

1. Fork the repository and create a branch from `main`.
2. Keep each pull request focused on one change.
3. Build with **zero warnings**: `arduino-cli compile --warnings all ...` (see the README).
4. Test on the watch when the change touches hardware, power or timing.
5. Describe what changed and how you tested it.

### Code style

- Format C/C++ with [`.clang-format`](.clang-format); editors pick up [`.editorconfig`](.editorconfig).
- Every `.cpp` file starts with a comment that explains what the module does, then includes its own header, then `<Arduino.h>`, then project headers.
- Comments explain *why*, not *what*. Code, comments and UI text are in English.
- Names: `snake_case` for functions and variables with the module prefix (`wled_set_color`), `UPPER_CASE` for constants and macros.

### Architecture rules

- **Layers:** `ui` → `services` → `drivers` → `core`. A layer only uses the layers below it.
- Drivers and services never touch LVGL objects. They publish state through the subjects in `core/settings.h` or through callbacks installed by the UI.
- LVGL is only used from the Arduino loop task. Background tasks store plain state and call `system_ui_wake()`.
- Every I²C transaction holds the shared lock: `I2CGuard lock;`.
- Large buffers go to PSRAM (`psram_malloc`); internal RAM is reserved for Wi-Fi, Bluetooth and DMA.

### Adding an app

1. Create `Smartwatch/src/ui/apps/<name>_app.cpp` with a `<name>_open()` function that builds a page with `kit_page_create()` and shows it with `kit_page_push()`.
2. Declare `<name>_open()` in `Smartwatch/src/ui/apps/apps.h`.
3. Add it to `LAUNCHER_APPS` in `Smartwatch/src/ui/system/launcher.cpp`.
4. Put logic that does not draw anything (timers, storage, network) into a service in `Smartwatch/src/services/`.

### Icons and fonts

Fonts and icons are generated. Add the Font Awesome 5 code point to `ICONS` in [`tools/gen_fonts.py`](tools/gen_fonts.py), run `python tools/gen_fonts.py` and use `ICON_<NAME>`. See [tools/README.md](tools/README.md).

## Code of conduct

Please be kind and respectful. See [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).
