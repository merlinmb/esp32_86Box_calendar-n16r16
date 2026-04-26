from PIL import Image

img = Image.open('../assets/logo3.jpg')
img = img.convert('RGB')
w, h = img.size
pixels = list(img.getdata())

data = []
for r, g, b in pixels:
    rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    data.append(rgb565 & 0xFF)
    data.append((rgb565 >> 8) & 0xFF)

lines = []
lines.append('#include "lvgl.h"')
lines.append('')
lines.append('static const uint8_t logo3_map[] = {')

row_data = []
for i, byte in enumerate(data):
    row_data.append('0x{:02X}'.format(byte))
    if (i + 1) % 16 == 0:
        lines.append('    ' + ', '.join(row_data) + ',')
        row_data = []
if row_data:
    lines.append('    ' + ', '.join(row_data))

lines.append('};')
lines.append('')
lines.append('const lv_img_dsc_t logo3_img = {')
lines.append('    .header = {')
lines.append('        .cf = LV_IMG_CF_TRUE_COLOR,')
lines.append('        .always_zero = 0,')
lines.append('        .reserved = 0,')
lines.append('        .w = {},'.format(w))
lines.append('        .h = {},'.format(h))
lines.append('    },')
lines.append('    .data_size = {},'.format(len(data)))
lines.append('    .data = logo3_map,')
lines.append('};')

output = '\n'.join(lines) + '\n'
with open('../src/logo3_img.c', 'w') as f:
    f.write(output)
print('Done. Image {}x{}, {} bytes'.format(w, h, len(data)))
