"""
render.py - Renders the watch UI on the PC and regenerates the documentation media:
docs/images/screens/*.png (every screen), docs/SCREENSHOTS.md (the gallery), the watch
mockups (faces.png, extras.png), the demo GIFs, banner.png and social-preview.png.

  python tools/preview/render.py              build, render everything, write the media
  python tools/preview/render.py --no-build   reuse the last build
  python tools/preview/render.py --no-media   only render the frames (build/frames)

Needs everything build.py needs, plus Pillow (pip install pillow).
"""
import argparse
import os
import subprocess
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

import build

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
FRAMES = os.path.join(HERE, "build", "frames")
DOCS = os.path.join(REPO, "docs")
MEDIA = os.path.join(DOCS, "images")
FONTS = os.path.join(REPO, "tools", "assets")
W, H, R = 368, 448, 44
ACCENT = (238, 30, 30)
S = 4  # supersampling for shapes

# Documented screens and their captions, in gallery order.
SCREENS = {
    "face-digital": "Digital", "face-analog": "Analog", "face-modular": "Modular", "face-minimal": "Minimal",
    "face-always-on": "Always-on display", "face-picker": "Watch face picker", "face-customize": "Customize",
    "tile-activity": "Activity tile", "tile-weather": "Weather tile", "tile-music": "Music tile",
    "system-launcher": "App launcher", "system-launcher-bottom": "App launcher with extras",
    "system-quick-panel": "Quick settings", "system-notification-banner": "Notification banner",
    "system-notifications": "Notification center", "system-notification-detail": "Notification",
    "system-incoming-call": "Incoming call", "system-find-my-watch": "Find my watch",
    "system-charging": "Charging", "system-power-menu": "Power menu",
    "app-activity": "Activity", "app-activity-history": "Activity goal and reminders",
    "app-workout": "Workout types", "app-workout-live": "Live workout", "app-workout-summary": "Workout summary",
    "app-sleep": "Last night", "app-sleep-week": "Bedtime schedule",
    "app-alarms": "Alarms", "app-alarm-edit": "Edit alarm", "app-alarm-ringing": "Alarm ringing",
    "app-timer": "Timer presets", "app-timer-running": "Timer running", "app-timer-done": "Timer done",
    "app-stopwatch": "Stopwatch with laps", "app-calendar": "Calendar",
    "app-weather": "Weather", "app-weather-details": "Details and next hours", "app-weather-forecast": "5-day forecast",
    "app-weather-location": "Location", "app-weather-search": "City search keyboard",
    "app-music": "Music player", "app-music-library": "Music library",
    "app-recorder": "Recorder", "app-recorder-recording": "Recording", "app-find-phone": "Find my phone",
    "extra-lights": "WLED lights", "extra-light-control": "Light control", "extra-light-effects": "Light effects",
    "extra-light-add": "Add a light", "extra-car": "Car control", "extra-car-more": "Car control (more)",
    "extra-car-live": "Car live data",
    "settings": "Settings", "settings-more": "Settings (more)", "settings-extras": "Extras",
    "settings-wifi": "Wi-Fi", "settings-wifi-password": "Wi-Fi password", "settings-bluetooth": "Bluetooth",
    "settings-display": "Display", "settings-display-wake": "Wake up", "settings-motion-sensor": "Motion sensor calibration",
    "settings-sound": "Sound", "settings-notifications": "Notifications", "settings-battery": "Battery",
    "settings-date-time": "Date and time", "settings-units": "Units", "settings-about": "About",
    "settings-about-more": "About (more)", "settings-system": "System", "settings-update": "Wireless update",
}
GALLERY = [("Watch faces and tiles", ("face-", "tile-")), ("System", ("system-",)), ("Apps", ("app-",)),
           ("Extras", ("extra-",)), ("Settings", ("settings",))]


def font(weight, size):
    return ImageFont.truetype(os.path.join(FONTS, f"Montserrat-{weight}.ttf"), size)


def rgb565(raw):
    out = bytearray(W * H * 3)
    mv = memoryview(raw)
    for i in range(W * H):
        v = mv[2 * i] | (mv[2 * i + 1] << 8)
        out[3 * i] = (((v >> 11) & 0x1F) * 527 + 23) >> 6
        out[3 * i + 1] = (((v >> 5) & 0x3F) * 259 + 33) >> 6
        out[3 * i + 2] = ((v & 0x1F) * 527 + 23) >> 6
    return Image.frombytes("RGB", (W, H), bytes(out))


def still(name):
    return rgb565(open(os.path.join(FRAMES, name + ".rgb565"), "rb").read())


def rounded_mask(size, radius):
    w, h = size
    big = Image.new("L", (w * S, h * S), 0)
    ImageDraw.Draw(big).rounded_rectangle((0, 0, w * S - 1, h * S - 1), radius * S, fill=255)
    return big.resize((w, h), Image.LANCZOS)


def vgradient(size, top, bottom):
    w, h = size
    g = Image.new("RGB", (1, h))
    for y in range(h):
        t = y / max(1, h - 1)
        g.putpixel((0, y), tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3)))
    return g.resize((w, h))


# ================================================================ Screens and gallery

def write_screens():
    out = os.path.join(MEDIA, "screens")
    os.makedirs(out, exist_ok=True)
    mask = rounded_mask((W, H), R)
    for name in SCREENS:
        img = still(name).convert("RGBA")
        img.putalpha(mask)
        img.save(os.path.join(out, name + ".png"), optimize=True)
    print(f"docs/images/screens: {len(SCREENS)} screens")


def write_gallery():
    lines = ["# Screenshots", "",
             "Every screen of the ESP32 Smartwatch firmware, rendered from the firmware's own UI code "
             "(LVGL 9, 368 × 448) with `tools/preview`.", "", "[← Back to the README](../README.md)", ""]
    for title, prefixes in GALLERY:
        items = [s for s in SCREENS if s.startswith(prefixes)]
        lines += [f"## {title}", "", "<table>"]
        for i in range(0, len(items), 4):
            lines.append("<tr>")
            for s in items[i:i + 4]:
                lines.append(f'<td align="center"><img src="images/screens/{s}.png" width="180" '
                             f'alt="ESP32 smartwatch: {SCREENS[s].lower()} screen"><br><sub>{SCREENS[s]}</sub></td>')
            lines.append("</tr>")
        lines += ["</table>", ""]
    open(os.path.join(DOCS, "SCREENSHOTS.md"), "w", encoding="utf-8", newline="\n").write("\n".join(lines))
    print("docs/SCREENSHOTS.md")


# ================================================================ Watch mockup

BEZEL = 22
RIM = 9
CASE_W, CASE_H = W + 2 * (BEZEL + RIM), H + 2 * (BEZEL + RIM)
STRAP_W = 292


def watch(screen, strap=110):
    """RGBA mockup: metal case, black glass, the screen, crown and fading straps."""
    pad_x = 20
    cw, ch = CASE_W + 2 * pad_x, CASE_H + 2 * strap
    img = Image.new("RGBA", (cw, ch), (0, 0, 0, 0))
    cx0, cy0 = pad_x, strap

    for top in (True, False):  # straps, behind the case
        sh = strap + 60
        band = vgradient((STRAP_W, sh), (22, 22, 25) if top else (30, 30, 34), (30, 30, 34) if top else (18, 18, 20))
        m = rounded_mask((STRAP_W, sh), 26)
        fade = Image.linear_gradient("L").resize((STRAP_W, sh))
        if not top:
            fade = fade.transpose(Image.FLIP_TOP_BOTTOM)
        m = ImageChops.multiply(m, fade.point(lambda v: min(255, v * 2)))
        img.paste(band, ((cw - STRAP_W) // 2, 0 if top else ch - sh), m)

    d = ImageDraw.Draw(img)  # crown and button
    crown_x = cx0 + CASE_W - 4
    cy = cy0 + CASE_H // 2
    for (y0, y1, w) in ((cy - 92, cy - 22, 16), (cy + 10, cy + 58, 10)):
        img.paste(vgradient((w, y1 - y0), (95, 95, 102), (40, 40, 45)), (crown_x, y0), rounded_mask((w, y1 - y0), 5))
    for yy in range(cy - 86, cy - 26, 6):
        d.line((crown_x + 3, yy, crown_x + 13, yy), fill=(55, 55, 60, 255), width=2)

    img.paste(vgradient((CASE_W, CASE_H), (78, 78, 84), (28, 28, 32)), (cx0, cy0),
              rounded_mask((CASE_W, CASE_H), R + BEZEL + RIM))
    img.paste(vgradient((CASE_W - 4, CASE_H - 4), (52, 52, 57), (22, 22, 25)), (cx0 + 2, cy0 + 2),
              rounded_mask((CASE_W - 4, CASE_H - 4), R + BEZEL + RIM - 2))
    gw, gh = W + 2 * BEZEL, H + 2 * BEZEL
    img.paste(Image.new("RGB", (gw, gh), (4, 4, 5)), (cx0 + RIM, cy0 + RIM), rounded_mask((gw, gh), R + BEZEL))
    img.paste(screen.convert("RGB"), (cx0 + RIM + BEZEL, cy0 + RIM + BEZEL), rounded_mask((W, H), R))
    refl = Image.new("L", (gw, gh), 0)  # soft reflection on the glass
    ImageDraw.Draw(refl).polygon([(0, 0), (gw * 0.55, 0), (0, gh * 0.45)], fill=12)
    refl = Image.composite(refl, Image.new("L", (gw, gh), 0), rounded_mask((gw, gh), R + BEZEL))
    white = Image.new("RGBA", (gw, gh), (255, 255, 255, 0))
    white.putalpha(refl)
    img.alpha_composite(white, (cx0 + RIM, cy0 + RIM))
    return img, (cx0 + RIM + BEZEL, cy0 + RIM + BEZEL)


def shadow(img, radius=26, opacity=150, offset=(0, 18)):
    """The image on a larger transparent canvas with a soft drop shadow."""
    pad = radius * 3 + max(offset)
    size = (img.width + 2 * pad, img.height + 2 * pad)
    a = Image.new("L", size, 0)
    a.paste(img.getchannel("A").point(lambda v: v * opacity // 255), (pad + offset[0], pad + offset[1]))
    canvas = Image.new("RGBA", size, (0, 0, 0, 0))
    canvas.putalpha(a.filter(ImageFilter.GaussianBlur(radius)))
    canvas.alpha_composite(img, (pad, pad))
    return canvas


def paste_over(canvas, shadowed, original, x, y):
    """Composites shadow() output so that `original` lands at (x, y), clipped to the canvas."""
    left = x - (shadowed.width - original.width) // 2
    top = y - (shadowed.height - original.height) // 2
    crop = shadowed.crop((max(0, -left), max(0, -top), shadowed.width, shadowed.height))
    crop = crop.crop((0, 0, min(crop.width, canvas.width - max(0, left)), min(crop.height, canvas.height - max(0, top))))
    canvas.alpha_composite(crop, (max(0, left), max(0, top)))


def background(size, glow_at=None, glow_r=420, glow=ACCENT, strength=70):
    w, h = size
    bg = vgradient(size, (14, 14, 18), (6, 6, 8)).convert("RGBA")
    if glow_at:
        g = Image.new("L", size, 0)
        gx, gy = glow_at
        ImageDraw.Draw(g).ellipse((gx - glow_r, gy - glow_r, gx + glow_r, gy + glow_r), fill=strength)
        layer = Image.new("RGBA", size, glow + (0,))
        layer.putalpha(g.filter(ImageFilter.GaussianBlur(glow_r // 2)))
        bg.alpha_composite(layer)
    dots = Image.new("RGBA", size, (0, 0, 0, 0))
    d = ImageDraw.Draw(dots)
    for y in range(18, h, 36):
        for x in range(18, w, 36):
            d.point((x, y), fill=(255, 255, 255, 22))
    bg.alpha_composite(dots)
    return bg


def place_watch(canvas, screen_name, x, y, scale, strap=90):
    mock, _ = watch(still(screen_name), strap=strap)
    mock = shadow(mock, 30, 190, (0, 26))
    if scale != 1:
        mock = mock.resize((int(mock.width * scale), int(mock.height * scale)), Image.LANCZOS)
    canvas.alpha_composite(mock, (int(x - mock.width / 2), int(y - mock.height / 2)))


# ================================================================ Animations

def touch_events(name):
    events = []
    for line in open(os.path.join(FRAMES, name + ".touch")):
        p = line.split()
        events.append((p[0], int(p[1]), [int(v) for v in p[2:]]))
    return events


def draw_touches(frame, index, events):
    """A fading ring where a finger tapped, a moving dot with a trail for swipes."""
    layer = Image.new("RGBA", (W * 2, H * 2), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    for kind, start, v in events:
        k = index - start
        if kind == "tap" and 0 <= k < 9:
            x, y = v[0] * 2, v[1] * 2
            t = k / 8
            r = int(44 + 26 * t)
            d.ellipse((x - r, y - r, x + r, y + r), outline=(255, 255, 255, int(170 * (1 - t))), width=5)
            if k < 4:
                d.ellipse((x - 36, y - 36, x + 36, y + 36), fill=(255, 255, 255, 90))
        if kind == "swipe" and 0 <= k < 6:
            x0, y0, x1, y1 = [c * 2 for c in v]
            t = 1 - (1 - min(1.0, k / 4)) ** 2
            x, y = x0 + (x1 - x0) * t, y0 + (y1 - y0) * t
            a = 150 if k < 5 else 60
            for j in range(1, 7):
                tt = max(0.0, t - j * 0.07)
                tx, ty = x0 + (x1 - x0) * tt, y0 + (y1 - y0) * tt
                rr = 36 - j * 4
                d.ellipse((tx - rr, ty - rr, tx + rr, ty + rr), fill=(255, 255, 255, max(0, a // 3 - j * 6)))
            d.ellipse((x - 38, y - 38, x + 38, y + 38), fill=(255, 255, 255, a))
    out = frame.convert("RGBA")
    out.alpha_composite(layer.resize((W, H), Image.LANCZOS))
    return out.convert("RGB")


def gif_stage(strap=60, margin=26):
    frame0, (sx, sy) = watch(Image.new("RGB", (W, H)), strap=strap)
    bw, bh = frame0.width + 2 * margin, frame0.height
    base = background((bw, bh), glow_at=(bw // 2, bh // 2), glow_r=260, strength=55)
    paste_over(base, shadow(frame0, 18, 160, (0, 10)), frame0, margin, 0)
    return base, (margin + sx, sy)


def save_gif(frames, durations, name, palette_samples):
    mosaic = Image.new("RGB", (frames[0].width * len(palette_samples), frames[0].height))
    for k, f in enumerate(palette_samples):
        mosaic.paste(f, (k * f.width, 0))
    pal = mosaic.quantize(colors=255, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    q = [f.quantize(palette=pal, dither=Image.Dither.NONE) for f in frames]
    path = os.path.join(MEDIA, name)
    q[0].save(path, save_all=True, append_images=q[1:], duration=durations, loop=0, optimize=False, disposal=1)
    print(f"docs/images/{name}: {len(frames)} frames, {os.path.getsize(path) // 1024} kB")


def make_gif(recording, name):
    raw = open(os.path.join(FRAMES, recording + ".raw"), "rb").read()
    events = touch_events(recording)
    base, pos = gif_stage()
    mask = rounded_mask((W, H), R)
    frames = []
    for i in range(len(raw) // (W * H * 2)):
        f = base.copy()
        f.paste(draw_touches(rgb565(raw[i * W * H * 2:(i + 1) * W * H * 2]), i, events), pos, mask)
        frames.append(f.convert("RGB"))
    save_gif(frames, 40, name, frames[:: max(1, len(frames) // 24)])


def make_faces_gif(name):
    shots = [still(n) for n in ("faceshot-0", "faceshot-1", "faceshot-2", "faceshot-3", "face-always-on")]
    base, pos = gif_stage()
    mask = rounded_mask((W, H), R)
    frames, durations = [], []
    for k, shot in enumerate(shots):
        nxt = shots[(k + 1) % len(shots)]
        for j in range(7):  # hold, then cross-fade to the next face
            f = base.copy()
            f.paste(Image.blend(shot, nxt, j / 7) if j else shot, pos, mask)
            frames.append(f.convert("RGB"))
            durations.append(1700 if j == 0 else 45)
    save_gif(frames, durations, name, frames[::7])


# ================================================================ Banner and showcases

def pill(img, x, y, text, f, fg=(235, 235, 240), bg=(255, 255, 255, 20), border=(255, 255, 255, 60)):
    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    tw = d.textlength(text, font=f)
    h = f.size + 22
    d.rounded_rectangle((x, y, x + tw + 36, y + h), h // 2, fill=bg, outline=border, width=2)
    img.alpha_composite(layer)
    ImageDraw.Draw(img).text((x + 18, y + h / 2), text, font=f, fill=fg, anchor="lm")
    return x + tw + 36


def make_banner(name, size, title_size, sub_size, left, watches, chip_rows):
    w, h = size
    img = background(size, glow_at=(int(w * 0.74), h // 2), glow_r=int(h * 0.62), strength=58)
    g = Image.new("L", size, 0)  # a second, cooler glow for depth
    ImageDraw.Draw(g).ellipse((-200, h - 260, 500, h + 300), fill=40)
    layer = Image.new("RGBA", size, (60, 90, 255, 0))
    layer.putalpha(g.filter(ImageFilter.GaussianBlur(160)))
    img.alpha_composite(layer)
    for screen, x, y, sc in watches:
        place_watch(img, screen, x, y, sc)
    d = ImageDraw.Draw(img)
    x, y = left
    tag_f = font("Bold", int(sub_size * 0.62))
    pill(img, x, y, "OPEN SOURCE  •  MIT", tag_f, fg=(255, 130, 130), bg=(238, 30, 30, 45), border=(238, 30, 30, 150))
    y += tag_f.size + 50
    tf = font("Bold", title_size)
    d.text((x, y), "ESP32", font=tf, fill=(255, 255, 255))
    d.text((x, y + title_size * 1.02), "Smartwatch", font=tf, fill=ACCENT)
    y += int(title_size * 2.2)
    sf = font("Medium", sub_size)
    for line in ("Full-featured firmware for the ESP32-S3", "AMOLED • LVGL 9 • Arduino"):
        d.text((x, y), line, font=sf, fill=(175, 175, 185))
        y += int(sub_size * 1.35)
    y += int(sub_size * 0.7)
    cf = font("Medium", int(sub_size * 0.68))
    for row in chip_rows:
        cx = x
        for chip in row:
            cx = pill(img, cx, y, chip, cf) + 12
        y += cf.size + 34
    img.convert("RGB").save(os.path.join(MEDIA, name), optimize=True)
    print(f"docs/images/{name}")


def make_showcase(name, screens, size):
    w, h = size
    img = background(size, glow_at=(w // 2, h // 2), glow_r=520, strength=60)
    step = w / (len(screens) + 0.2)
    for k, screen in enumerate(screens):
        place_watch(img, screen, step * (k + 0.6), h / 2, 0.78, strap=110)
    img.convert("RGB").save(os.path.join(MEDIA, name), optimize=True)
    print(f"docs/images/{name}")


def write_media():
    os.makedirs(MEDIA, exist_ok=True)
    write_screens()
    write_gallery()
    make_banner("banner.png", (1600, 640), 104, 34, (96, 92),
                [("system-launcher", 1030, 330, 0.66), ("app-weather", 1420, 330, 0.66), ("face-digital", 1225, 330, 0.8)],
                [["Wi-Fi", "Bluetooth LE", "Gadgetbridge"], ["WLED", "Car control", "OTA"]])
    make_banner("social-preview.png", (1280, 640), 88, 28, (70, 96),
                [("system-launcher", 840, 330, 0.56), ("app-weather", 1170, 330, 0.56), ("face-digital", 1005, 330, 0.68)],
                [["Wi-Fi", "Bluetooth LE", "Gadgetbridge"], ["WLED", "Car control"]])
    make_showcase("faces.png", ["face-digital", "face-analog", "face-modular", "face-minimal", "face-always-on"],
                  (2000, 780))
    make_showcase("extras.png", ["extra-lights", "extra-light-control", "extra-car", "extra-car-live"], (1700, 780))
    make_gif("tour", "demo-navigation.gif")
    make_gif("apps", "demo-apps.gif")
    make_gif("extras", "demo-extras.gif")
    make_faces_gif("demo-watch-faces.gif")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--no-build", action="store_true", help="reuse the last build")
    ap.add_argument("--no-media", action="store_true", help="only render the frames")
    ap.add_argument("--scenes", default="fsaxcr", help="f faces, s system, a apps, x extras, c settings, r recordings")
    args = ap.parse_args()
    if not args.no_build:
        build.main()
    os.makedirs(FRAMES, exist_ok=True)
    r = subprocess.run([build.EXE, args.scenes], cwd=os.path.dirname(FRAMES), capture_output=True, text=True)
    print(r.stdout.strip().splitlines()[-1] if r.stdout.strip() else r.stderr)
    if r.returncode or "0 failed" not in r.stdout:
        print(r.stdout)
        sys.exit("preview run failed")
    if not args.no_media:
        write_media()


if __name__ == "__main__":
    main()
