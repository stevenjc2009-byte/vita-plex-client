"""Rasterize OFL-licensed Open Sans into small Vita runtime font atlases.
Usage: python tools/build-fonts.py <OpenSans.ttf>
Font source: https://github.com/google/fonts/tree/main/ofl/opensans
"""
import pathlib, struct, sys
from PIL import Image, ImageDraw, ImageFont
root = pathlib.Path(__file__).resolve().parents[1]
for size in (16, 20, 28):
    font = ImageFont.truetype(sys.argv[1], size)
    w = h = size + 12
    widths, pixels = bytearray(), bytearray()
    codes = list(range(32, 592)) + list(range(880, 1280)) + list(range(8192, 8304))
    for code in codes:
        ch = chr(code)
        image = Image.new('L', (w, h))
        ImageDraw.Draw(image).text((0, 0), ch, font=font, fill=255)
        widths.append(min(w, round(font.getlength(ch))))
        pixels.extend(image.tobytes())
    data = b'PFN2' + struct.pack('<HHHH', w, h, len(codes), 0) + b''.join(struct.pack('<I',code) for code in codes) + widths + pixels
    (root / 'assets' / f'font{size}.bin').write_bytes(data)
    print(f'font{size}.bin: {len(data)} bytes')
