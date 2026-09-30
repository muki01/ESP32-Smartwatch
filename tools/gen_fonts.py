"""
gen_fonts.py - Generates the fonts of the watch UI into Smartwatch/src/assets/fonts:
font_*.c (lv_font_conv, LVGL format), fonts.h (declarations) and icons.h (ICON_* strings).

  python tools/gen_fonts.py                  all fonts
  python tools/gen_fonts.py font_icons_24    only the named fonts (headers are always written)

Needs lv_font_conv (npm i -g lv_font_conv, or the lv_font_conv-win.exe shipped with
SquareLine Studio) and the Font Awesome 5 Free woff that comes with the LVGL library
(lvgl/scripts/generators/built_in_font). Set LV_FONT_CONV / FA_FONT when they are not
found automatically. The Montserrat TTFs are in tools/assets.

After changing ICONS, run it and rebuild: the check at the end lists icons that the
firmware uses but the list lacks (compile error) and icons nobody uses (wasted flash).
"""
import glob
import os
import re
import shutil
import subprocess
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(TOOLS)
SRC = os.path.join(REPO, "Smartwatch", "src")
OUT = os.path.join(SRC, "assets", "fonts")
ASSETS = os.path.join(TOOLS, "assets")
MONTSERRAT_LIGHT = os.path.join(ASSETS, "Montserrat-Light.ttf")
MONTSERRAT_MEDIUM = os.path.join(ASSETS, "Montserrat-Medium.ttf")
MONTSERRAT_BOLD = os.path.join(ASSETS, "Montserrat-Bold.ttf")

ICON_SIZES = (24, 32, 48)
BIG_SIZE = 96
NUM_SIZES = (44, 64, 110)
TEXT_SIZES = (20, 24, 28, 32)

# name, Font Awesome 5 Free code point (fa-name when it differs)
ICONS = [
    # Apps
    ("ACTIVITY", 0xF554),       # walking
    ("DUMBBELL", 0xF44B),       # workout
    ("BED", 0xF236),            # sleep, bedtime
    ("BELL", 0xF0F3),           # alarm, notifications
    ("TIMER", 0xF252),          # hourglass-half
    ("STOPWATCH", 0xF2F2),
    ("MUSIC", 0xF001),
    ("MICROPHONE", 0xF130),     # recorder
    ("CALENDAR", 0xF073),
    ("LIGHTS", 0xF75B),         # rainbow: WLED lights
    ("CAR", 0xF1B9),
    ("SETTINGS", 0xF013),       # cog
    ("PUZZLE", 0xF12E),         # puzzle-piece: extras
    # Status, quick panel, settings
    ("WIFI", 0xF1EB),
    ("BLUETOOTH", 0xF294),      # bluetooth-b
    ("MOON", 0xF186),           # do not disturb, night
    ("VOLUME", 0xF028),         # volume-up
    ("VOLUME_DOWN", 0xF027),
    ("MUTE", 0xF6A9),           # volume-mute
    ("EYE", 0xF06E),            # always-on display
    ("LEAF", 0xF06C),           # battery saver
    ("HAND", 0xF256),           # hand-paper: raise to wake, clap control
    ("FLASHLIGHT", 0xF0EB),     # lightbulb
    ("MOBILE", 0xF3CD),         # find phone, phone link
    ("POWER", 0xF011),
    ("LOCK", 0xF023),
    ("CLOCK", 0xF017),
    ("IMAGE", 0xF03E),          # watch faces
    ("SD_CARD", 0xF7C2),
    ("DOWNLOAD", 0xF019),       # updates
    ("INFO", 0xF129),
    ("WARNING", 0xF071),        # exclamation-triangle
    ("RULER", 0xF547),          # ruler-horizontal: units
    ("BATTERY_FULL", 0xF240),
    ("BATTERY_3", 0xF241),
    ("BATTERY_2", 0xF242),
    ("BATTERY_1", 0xF243),
    ("BATTERY_EMPTY", 0xF244),
    ("BOLT", 0xF0E7),           # charging, thunderstorm
    # Messages and calls
    ("PHONE", 0xF095),
    ("COMMENT", 0xF075),
    ("ENVELOPE", 0xF0E0),
    # Weather
    ("SUN", 0xF185),
    ("CLOUD_SUN", 0xF6C4),
    ("CLOUD_MOON", 0xF6C3),
    ("CLOUD", 0xF0C2),
    ("CLOUD_RAIN", 0xF73D),
    ("CLOUD_SHOWERS", 0xF740),  # cloud-showers-heavy
    ("SNOWFLAKE", 0xF2DC),
    ("SMOG", 0xF75F),
    ("WIND", 0xF72E),
    ("TINT", 0xF043),           # humidity
    ("THERMOMETER", 0xF2C9),    # thermometer-half
    ("MAP_MARKER", 0xF3C5),     # map-marker-alt
    ("LOCATION", 0xF124),       # location-arrow
    # Activity and workouts
    ("STEPS", 0xF54B),          # shoe-prints
    ("FIRE", 0xF06D),
    ("ROUTE", 0xF4D7),
    ("FLAG", 0xF024),
    ("RUNNING", 0xF70C),
    ("HIKING", 0xF6EC),
    ("HISTORY", 0xF1DA),
    # Car
    ("PARKING", 0xF540),
    ("TACHOMETER", 0xF3FD),     # tachometer-alt
    ("ROAD", 0xF018),
    ("WATER", 0xF773),
    # Controls
    ("PLAY", 0xF04B),
    ("PAUSE", 0xF04C),
    ("STOP", 0xF04D),
    ("PREV", 0xF048),           # step-backward
    ("NEXT", 0xF051),           # step-forward
    ("REPEAT", 0xF01E),         # redo
    ("LIST", 0xF03A),
    ("UNDO", 0xF0E2),
    ("SYNC", 0xF021),
    ("SEARCH", 0xF002),
    ("PLUS", 0xF067),
    ("CHECK", 0xF00C),
    ("TIMES", 0xF00D),
    ("TRASH", 0xF2ED),          # trash-alt
    ("CHEVRON_LEFT", 0xF053),
    ("CHEVRON_RIGHT", 0xF054),
    ("ARROW_LEFT", 0xF060),
    ("ARROW_RIGHT", 0xF061),
]

# The large weather glyphs of the Weather app.
BIG_ICONS = ["SUN", "MOON", "CLOUD_SUN", "CLOUD_MOON", "CLOUD", "CLOUD_RAIN", "CLOUD_SHOWERS",
             "SNOWFLAKE", "BOLT", "SMOG"]


def find_tool(env, candidates, what):
    path = os.environ.get(env)
    if path:
        return path
    for c in candidates:
        found = shutil.which(c) if not os.path.isabs(c) else (c if os.path.exists(c) else None)
        if found:
            return found
        for match in glob.glob(c):
            return match
    sys.exit(f"{what} not found: set {env}")


def utf8_escape(cp):
    return "".join(f"\\x{b:02X}" for b in chr(cp).encode("utf-8"))


def convert(font_conv, name, size, font_args, fallback, strip_paths):
    out = os.path.join(OUT, name + ".c")
    cmd = [font_conv, "--bpp", "4", "--size", str(size), "--no-compress"] + font_args + [
        "--format", "lvgl", "--lv-include", "lvgl.h", "--lv-font-name", name, "-o", out]
    if fallback:
        cmd += ["--lv-fallback", fallback]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        print(r.stdout, r.stderr)
        sys.exit(f"lv_font_conv failed for {name}")
    if r.stdout.strip() or r.stderr.strip():
        print(r.stdout.strip(), r.stderr.strip())
    # Keep machine-specific paths out of the generated sources.
    text = open(out, encoding="utf-8").read()
    for path in strip_paths:
        text = text.replace(path + os.sep, "").replace(path.replace("\\", "/") + "/", "")
    open(out, "w", encoding="utf-8", newline="\n").write(text)
    print(f"  {name}.c  {os.path.getsize(out) // 1024} kB")


def write_headers():
    decls = [f"LV_FONT_DECLARE(font_text_{s})" for s in TEXT_SIZES]
    decls += ["LV_FONT_DECLARE(font_bold_28)"]
    decls += [f"LV_FONT_DECLARE(font_num_{s})" for s in NUM_SIZES]
    decls += ["LV_FONT_DECLARE(font_light_180)"]
    decls += [f"LV_FONT_DECLARE(font_icons_{s})" for s in ICON_SIZES + (BIG_SIZE,)]
    fonts_h = [
        "/*",
        " * fonts.h - Fonts of the watch UI. Generated by tools/gen_fonts.py: do not edit.",
        " *",
        " *   font_text_20/24/28/32   Montserrat Medium: Latin-1, Latin Extended-A (Turkish), Cyrillic",
        " *   font_bold_28            Montserrat Bold ASCII and the degree sign (watch face labels)",
        " *   font_num_44/64/110      Montserrat Light digits and : . , / % ° + - (clocks, timers);",
        " *                           other characters fall back to font_text_32",
        " *   font_light_180          Montserrat Light digits, ':' and '-' (hours of the Digital face)",
        " *   font_icons_24/32/48     Font Awesome 5 Free icons (icons.h); they fall back to the",
        " *                           built-in Montserrat, so LV_SYMBOL_* and plain text work too",
        " *   font_icons_96           large weather glyphs (" + ", ".join(BIG_ICONS) + ")",
        " */",
        "#pragma once",
        "",
        '#include "lvgl.h"',
        "",
        "#ifdef __cplusplus",
        'extern "C" {',
        "#endif",
        "",
    ] + decls + [
        "",
        "#ifdef __cplusplus",
        "}",
        "#endif",
        "",
    ]
    open(os.path.join(OUT, "fonts.h"), "w", encoding="utf-8", newline="\n").write("\n".join(fonts_h))

    width = max(len(n) for n, _ in ICONS)
    icons_h = [
        "/*",
        " * icons.h - Icon glyphs of the watch UI: Font Awesome 5 Free (SIL OFL 1.1) code points as",
        " * UTF-8 strings, for font_icons_24/32/48 (fonts.h). Generated by tools/gen_fonts.py: do not",
        " * edit, add icons to the list in the script.",
        " */",
        "#pragma once",
        "",
    ] + [f'#define ICON_{n:<{width}} "{utf8_escape(cp)}"  /* U+{cp:04X} */' for n, cp in ICONS] + [""]
    open(os.path.join(OUT, "icons.h"), "w", encoding="utf-8", newline="\n").write("\n".join(icons_h))


def check_usage():
    used = set()
    for path in glob.glob(os.path.join(SRC, "**", "*.*"), recursive=True):
        if path.startswith(os.path.join(SRC, "assets")) or not path.endswith((".c", ".cpp", ".h")):
            continue
        used |= set(re.findall(r"\bICON_([A-Z0-9_]+)\b", open(path, encoding="utf-8", errors="ignore").read()))
    listed = {n for n, _ in ICONS}
    missing = sorted(used - listed)
    unused = sorted(listed - used)
    if missing:
        print("MISSING (used by the firmware, not in ICONS):", ", ".join(missing))
    if unused:
        print("unused (in ICONS, used nowhere):", ", ".join(unused))
    return not missing


def main():
    only = set(sys.argv[1:])
    font_conv = find_tool("LV_FONT_CONV", ["lv_font_conv", r"D:\SquareLine Studio*\lvgl\lv_font_conv-win.exe",
                                           r"C:\Program Files\SquareLine Studio*\lvgl\lv_font_conv-win.exe"],
                          "lv_font_conv")
    fa = find_tool("FA_FONT", [os.path.join(os.path.expanduser("~"), "Documents", "Arduino", "libraries", "lvgl",
                                            "scripts", "generators", "built_in_font",
                                            "FontAwesome5-Solid+Brands+Regular.woff"),
                               os.path.join(os.path.expanduser("~"), "Arduino", "libraries", "lvgl", "scripts",
                                            "generators", "built_in_font", "FontAwesome5-Solid+Brands+Regular.woff")],
                   "Font Awesome 5 woff")
    strip = [os.path.dirname(fa), ASSETS, OUT]
    codes = dict(ICONS)

    def run(name, size, args, fallback):
        if not only or name in only:
            convert(font_conv, name, size, args, fallback, strip)

    print("Fonts ->", os.path.relpath(OUT, REPO))
    icon_range = ",".join(f"0x{cp:X}" for _, cp in ICONS)
    for size in ICON_SIZES:
        run(f"font_icons_{size}", size, ["--font", fa, "-r", icon_range], f"lv_font_montserrat_{min(size, 32)}")
    run(f"font_icons_{BIG_SIZE}", BIG_SIZE, ["--font", fa, "-r", ",".join(f"0x{codes[n]:X}" for n in BIG_ICONS)],
        None)

    numerals = "0x20,0x25,0x2B-0x3A,0xB0"  # space % + , - . / 0-9 : °
    for size in NUM_SIZES:
        run(f"font_num_{size}", size, ["--font", MONTSERRAT_LIGHT, "-r", numerals], "font_text_32")
    run("font_light_180", 180, ["--font", MONTSERRAT_LIGHT, "-r", "0x20,0x2D,0x30-0x3A"], None)
    run("font_bold_28", 28, ["--font", MONTSERRAT_BOLD, "-r", "0x20-0x7E,0xB0"], "font_text_28")

    # Latin-1 + Latin Extended-A (Turkish, most European languages) + basic Cyrillic and
    # typographic punctuation. LV_SYMBOL_* glyphs fall back to the built-in Montserrat.
    text_ranges = "0x20-0x7E,0xA0-0x17F,0x400-0x45F,0x2013-0x2014,0x2018-0x201E,0x2022,0x2026,0x20AC"
    for size in TEXT_SIZES:
        run(f"font_text_{size}", size, ["--font", MONTSERRAT_MEDIUM, "-r", text_ranges], f"lv_font_montserrat_{size}")

    write_headers()
    print(f"  fonts.h, icons.h ({len(ICONS)} icons)")
    if not check_usage():
        sys.exit(1)


if __name__ == "__main__":
    main()
