"""Create synthetic poster fixtures for the native renderer's desktop preview."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import sys
root=Path(__file__).resolve().parents[1]
out=root/'build'/'preview-art';out.mkdir(parents=True,exist_ok=True)
font=ImageFont.truetype(sys.argv[1],19)
palettes=[((19,48,57),(169,116,69)),((31,24,55),(124,78,132)),((20,65,58),(170,147,90)),
 ((42,26,19),(181,107,42)),((24,34,55),(78,118,155)),((23,42,56),(176,168,143)),
 ((41,42,40),(131,119,90)),((56,43,25),(218,153,45)),((39,37,66),(153,103,117)),((26,46,64),(64,108,137))]
for i,(top,bottom) in enumerate(palettes):
    im=Image.new('RGB',(160,240));d=ImageDraw.Draw(im)
    for y in range(240):d.line((0,y,159,y),fill=tuple(round(a+(b-a)*y/240) for a,b in zip(top,bottom)))
    d.ellipse((50,28,135,113),outline=(228,216,180),width=2)
    d.polygon([(0,186),(50,82),(112,180),(160,125),(160,240),(0,240)],fill=tuple(v//2 for v in top))
    d.text((15,185),'PLEX',font=font,fill=(245,237,220));d.text((15,211),f'PREVIEW {i+1:02}',font=ImageFont.truetype(sys.argv[1],11),fill=(224,207,174))
    value=2166136261
    for byte in f'http://preview:32400/sample/{i}'.encode():value=((value^byte)*16777619)&0xffffffff
    im.save(out/f'{value:08x}.jpg',quality=90)
