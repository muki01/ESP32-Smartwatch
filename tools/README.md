# tools

Developer tools. None of them is needed to build the firmware: the generated files are
committed.

| Tool | Writes | Input |
| ---- | ------ | ----- |
| `gen_fonts.py` | `Smartwatch/src/assets/fonts/font_*.c`, `fonts.h`, `icons.h` | `assets/Montserrat-*.ttf`, Font Awesome 5 Free |
| `gen_images.py` | `Smartwatch/src/assets/images/img_*.c`, `images.h` | `assets/*.png` |
| `preview/` | `docs/images/**`, `docs/SCREENSHOTS.md` | the firmware's UI sources |

## Fonts and images

```sh
python tools/gen_fonts.py                 # all fonts
python tools/gen_fonts.py font_icons_24   # only the named fonts
python tools/gen_images.py
```

Requirements:

- Python 3.8+, Pillow for images (`pip install pillow`)
- [lv_font_conv](https://github.com/lvgl/lv_font_conv) (`npm i -g lv_font_conv`, or the
  `lv_font_conv-win.exe` that ships with SquareLine Studio). Set `LV_FONT_CONV` to its path
  when it is not on `PATH`.
- `FontAwesome5-Solid+Brands+Regular.woff` from the LVGL library
  (`lvgl/scripts/generators/built_in_font`). Set `FA_FONT` when the Arduino libraries are
  not in the default folder.

### Adding an icon

1. Pick a Font Awesome 5 Free (solid) icon and add `("NAME", 0xF...)` to `ICONS` in
   `gen_fonts.py`.
2. Run `python tools/gen_fonts.py` and use `ICON_NAME` with `font_icons_24/32/48`.

The script ends with a check of the firmware sources: icons used there but missing from the
list fail the run, icons nobody uses are listed so they can be dropped (each one costs flash
in every icon size).

## PC preview

`preview/` compiles the firmware's real UI, settings and logic services for your computer,
with the drivers, radios and network replaced by stubs that return sample data
(`preview/stubs.cpp`). It renders every screen headlessly and turns the frames into the
documentation media.

```sh
pip install ziglang pillow
python tools/preview/render.py              # build, render, write docs/images and docs/SCREENSHOTS.md
python tools/preview/render.py --no-media   # only render the frames into tools/preview/build/frames
```

- The compiler is [zig](https://ziglang.org) (`pip install ziglang`), so no other C/C++
  toolchain is needed. Windows, Linux and macOS.
- LVGL and ArduinoJson come from the Arduino libraries folder (`~/Documents/Arduino/libraries`
  or `~/Arduino/libraries`, or set `ARDUINO_LIBRARIES`).
- The LVGL configuration is the firmware's own `lv_conf.h` with three host changes.
- `preview/main.cpp` holds the scenes: sample data, the screens to render and the recorded
  sequences for the GIFs (taps and swipes are drawn on the frames).

Use it to check UI changes before flashing: after a run, every screen is a PNG in
`docs/images/screens`, ready to be reviewed or attached to a pull request.

## Licenses

Montserrat: SIL Open Font License 1.1. Font Awesome 5 Free icons: SIL OFL 1.1 (fonts),
CC BY 4.0 (icons).
