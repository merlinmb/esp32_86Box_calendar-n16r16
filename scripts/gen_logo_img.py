Import("env")
import os
import struct

PROJECT_DIR = env.subst("$PROJECT_DIR")
SRC_DIR     = env.subst("$PROJECT_SRC_DIR")

JPG_PATH = os.path.join(PROJECT_DIR, "assets", "logo3.jpg")
OUT_PATH = os.path.join(SRC_DIR, "logo3_img.c")


def jpeg_dimensions(path):
    with open(path, "rb") as f:
        data = f.read()
    i = 0
    while i < len(data) - 4:
        if data[i] != 0xFF:
            i += 1
            continue
        marker = data[i + 1]
        if marker in (0xC0, 0xC1, 0xC2, 0xC3):
            h = struct.unpack(">H", data[i + 5:i + 7])[0]
            w = struct.unpack(">H", data[i + 7:i + 9])[0]
            return w, h
        elif marker in (0xD8, 0xD9):
            i += 2
        else:
            length = struct.unpack(">H", data[i + 2:i + 4])[0]
            i += 2 + length
    raise RuntimeError("Could not find JPEG SOF marker in " + path)


def needs_regen():
    if not os.path.exists(OUT_PATH):
        return True
    return os.path.getmtime(JPG_PATH) > os.path.getmtime(OUT_PATH)


def convert_logo(source, target, env):
    if not needs_regen():
        print("gen_logo_img: logo3_img.c is up to date, skipping.")
        return

    try:
        from PIL import Image
    except ImportError:
        import subprocess, sys
        subprocess.check_call([sys.executable, "-m", "pip", "install", "Pillow", "-q"])
        from PIL import Image

    img = Image.open(JPG_PATH).convert("RGB")
    w, h = img.size
    pixels = list(img.getdata())

    data = []
    for r, g, b in pixels:
        rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        data.append(rgb565 & 0xFF)
        data.append((rgb565 >> 8) & 0xFF)

    lines = [
        "#include \"lvgl.h\"",
        "",
        "static const uint8_t logo3_map[] = {",
    ]
    row = []
    for i, byte in enumerate(data):
        row.append("0x{:02X}".format(byte))
        if (i + 1) % 16 == 0:
            lines.append("    " + ", ".join(row) + ",")
            row = []
    if row:
        lines.append("    " + ", ".join(row))
    lines += [
        "};",
        "",
        "const lv_img_dsc_t logo3_img = {",
        "    .header = {",
        "        .cf = LV_IMG_CF_TRUE_COLOR,",
        "        .always_zero = 0,",
        "        .reserved = 0,",
        "        .w = {},".format(w),
        "        .h = {},".format(h),
        "    },",
        "    .data_size = {},".format(len(data)),
        "    .data = logo3_map,",
        "};",
        "",
    ]

    with open(OUT_PATH, "w") as f:
        f.write("\n".join(lines))

    print("gen_logo_img: generated {} ({}x{}, {} bytes)".format(
        os.path.relpath(OUT_PATH, PROJECT_DIR), w, h, len(data)))


env.AddPreAction("$BUILD_DIR/src/logo3_img.c.o", convert_logo)
