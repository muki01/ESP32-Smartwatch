"""
build.py - Builds the PC preview of the watch UI: LVGL, the firmware's UI, settings and
logic services, the generated fonts and images, and stubs for the hardware (stubs.cpp).

  python tools/preview/build.py

Needs zig as the C/C++ compiler (pip install ziglang) and the LVGL and ArduinoJson
libraries of the Arduino IDE (found in ~/Documents/Arduino/libraries or ~/Arduino/libraries;
set ARDUINO_LIBRARIES to another folder). The LVGL configuration is the firmware's own
lv_conf.h with three host changes (C library heap, no ESP32 attributes, printf logging).
"""
import concurrent.futures
import glob
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
SRC = os.path.join(REPO, "Smartwatch", "src").replace("\\", "/")
BUILD = os.path.join(HERE, "build")
CONF = os.path.join(BUILD, "conf")
EXE = os.path.join(BUILD, "preview.exe" if os.name == "nt" else "preview")
ZIG = [sys.executable, "-m", "ziglang"]

# Real firmware sources; everything hardware or network bound is in stubs.cpp.
FIRMWARE = ["core/settings.cpp", "core/format.cpp"] + [
    "services/" + s + ".cpp" for s in ("activity", "agenda", "alarms", "countdown", "notifications", "sleep_tracker",
                                        "workout", "battery_saver", "weather", "phone", "clap_control")]
HOST_CONF = [
    ("#define LV_USE_STDLIB_MALLOC LV_STDLIB_CUSTOM", "#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB  //"),
    ("#define LV_ATTRIBUTE_USE_CUSTOM_INCLUDE 1", "#define LV_ATTRIBUTE_USE_CUSTOM_INCLUDE 0"),
    ("#define LV_LOG_PRINTF 0", "#define LV_LOG_PRINTF 1"),
]


def libraries():
    candidates = [os.environ.get("ARDUINO_LIBRARIES", "")]
    home = os.path.expanduser("~")
    candidates += [os.path.join(home, "Documents", "Arduino", "libraries"), os.path.join(home, "Arduino", "libraries")]
    for c in candidates:
        if c and os.path.isdir(os.path.join(c, "lvgl")) and os.path.isdir(os.path.join(c, "ArduinoJson")):
            return c.replace("\\", "/")
    sys.exit("LVGL / ArduinoJson not found: set ARDUINO_LIBRARIES to the Arduino libraries folder")


def write_host_conf():
    text = open(os.path.join(REPO, "Smartwatch", "lv_conf.h"), encoding="utf-8").read()
    for old, new in HOST_CONF:
        if text.count(old) != 1:
            sys.exit(f"lv_conf.h: '{old}' not found")
        text = text.replace(old, new)
    os.makedirs(CONF, exist_ok=True)
    path = os.path.join(CONF, "lv_conf.h")
    if not os.path.exists(path) or open(path, encoding="utf-8").read() != text:
        open(path, "w", encoding="utf-8", newline="\n").write(text)


def run(cmd):
    r = subprocess.run(cmd, cwd=BUILD, capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def obj_path(src, folder):
    return os.path.join(folder, src.replace(":", "_").replace("/", "_").replace("\\", "_") + ".o")


def main():
    libs = libraries()
    lvgl = libs + "/lvgl"
    c_flags = ["-O1", "-w", "-DLV_CONF_INCLUDE_SIMPLE", "-I" + CONF, "-I" + lvgl, "-I" + lvgl + "/src"]
    cxx_flags = ["-O1", "-w", "-std=c++17", "-DLV_CONF_INCLUDE_SIMPLE", "-I" + CONF, "-I" + os.path.join(HERE, "stubs"),
                 "-I" + lvgl, "-I" + lvgl + "/src", "-I" + libs + "/ArduinoJson/src", "-I" + SRC, "-I" + HERE]
    for d in ("obj", "lvobj", "frames"):
        os.makedirs(os.path.join(BUILD, d), exist_ok=True)
    write_host_conf()
    conf_time = os.path.getmtime(os.path.join(CONF, "lv_conf.h"))

    def compile_c(src):  # LVGL and the generated assets: cached
        obj = obj_path(src, os.path.join(BUILD, "lvobj"))
        if os.path.exists(obj) and os.path.getmtime(obj) > max(os.path.getmtime(src), conf_time):
            return 0, "", obj
        code, out = run(ZIG + ["cc", "-c"] + c_flags + [src, "-o", obj])
        return code, (src + ":\n" + out) if code else "", obj

    def compile_cxx(src):
        obj = obj_path(src, os.path.join(BUILD, "obj"))
        code, out = run(ZIG + ["c++", "-c"] + cxx_flags + [src, "-o", obj])
        return code, (src + ":\n" + out) if code else "", obj

    c_sources = [p.replace("\\", "/") for p in glob.glob(lvgl + "/src/**/*.c", recursive=True)]
    c_sources += [p.replace("\\", "/") for p in glob.glob(SRC + "/assets/**/*.c", recursive=True)]
    cxx_sources = [p.replace("\\", "/") for p in glob.glob(SRC + "/ui/**/*.cpp", recursive=True)]
    cxx_sources += [SRC + "/" + s for s in FIRMWARE]
    cxx_sources += [os.path.join(HERE, "stubs.cpp"), os.path.join(HERE, "main.cpp")]
    objs, failed = [], False
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count() or 4) as pool:
        for code, out, obj in list(pool.map(compile_c, c_sources)) + list(pool.map(compile_cxx, cxx_sources)):
            if code:
                print(out[:4000])
                failed = True
            objs.append(obj)
    if failed:
        sys.exit("compile failed")
    rsp = os.path.join(BUILD, "objs.rsp")  # the object list is longer than a Windows command line
    with open(rsp, "w") as f:
        f.write("\n".join('"' + o.replace("\\", "/") + '"' for o in objs))
    code, out = run(ZIG + ["c++", "@" + rsp, "-o", EXE])
    if code:
        print(out[:12000])
        sys.exit("link failed")
    print("built", os.path.relpath(EXE, REPO))


if __name__ == "__main__":
    main()
