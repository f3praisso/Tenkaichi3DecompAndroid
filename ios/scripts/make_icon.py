"""O icone do app: gerado aqui, sem imagem de ninguem. Fundo azul-violeta, um brilho laranja e as letras BT3."""
import os, sys
from PIL import Image, ImageDraw, ImageFont, ImageFilter

S = 1024
out = sys.argv[1] if len(sys.argv) > 1 else "."

# fundo: degrade vertical
bg = Image.new("RGB", (S, S))
d = ImageDraw.Draw(bg)
for y in range(S):
    t = y / (S - 1)
    d.line([(0, y), (S, y)], fill=(int(14 + 52 * t), int(18 - 4 * t), int(56 + 54 * t)))

# brilho laranja atras das letras
rg = Image.radial_gradient("L").resize((S, S)).point(lambda v: 255 - v)
mask = rg.point(lambda v: int((v / 255.0) ** 2.0 * 235))
bg.paste(Image.new("RGB", (S, S), (255, 138, 20)), (0, 0), mask)

# faixas de luz (raios) sutis
rays = Image.new("RGBA", (S, S), (0, 0, 0, 0))
rd = ImageDraw.Draw(rays)
import math
for k in range(12):
    a = math.radians(k * 30 + 8)
    x1, y1 = S / 2 + math.cos(a) * S, S / 2 + math.sin(a) * S
    a2 = a + math.radians(5)
    x2, y2 = S / 2 + math.cos(a2) * S, S / 2 + math.sin(a2) * S
    rd.polygon([(S / 2, S / 2), (x1, y1), (x2, y2)], fill=(255, 190, 80, 28))
bg = Image.alpha_composite(bg.convert("RGBA"), rays).convert("RGB")

# letras
cands = [os.environ.get("BT3_ICON_FONT", ""), "/System/Library/Fonts/Supplemental/Impact.ttf", "/System/Library/Fonts/Supplemental/Arial Black.ttf",
         "/System/Library/Fonts/Supplemental/Arial Bold.ttf", "/System/Library/Fonts/Helvetica.ttc", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"]
font = None
for c in cands:
    if c and os.path.exists(c):
        font = ImageFont.truetype(c, 430)
        break
if font is None:
    font = ImageFont.load_default()
txt = "BT3"
d = ImageDraw.Draw(bg)
bb = d.textbbox((0, 0), txt, font=font, stroke_width=18)
w, h = bb[2] - bb[0], bb[3] - bb[1]
x, y = (S - w) // 2 - bb[0], (S - h) // 2 - bb[1] - 10
sh = Image.new("RGBA", (S, S), (0, 0, 0, 0))
ImageDraw.Draw(sh).text((x + 10, y + 18), txt, font=font, fill=(0, 0, 0, 170), stroke_width=18, stroke_fill=(0, 0, 0, 170))
sh = sh.filter(ImageFilter.GaussianBlur(14))
bg = Image.alpha_composite(bg.convert("RGBA"), sh).convert("RGB")
d = ImageDraw.Draw(bg)
d.text((x, y), txt, font=font, fill=(255, 255, 255), stroke_width=18, stroke_fill=(255, 105, 0))

# o iOS pede icones opacos e quadrados (ele mesmo arredonda os cantos)
bg.save(os.path.join(out, "AppIcon1024.png"))
for size, name in ((120, "AppIcon60x60@2x.png"), (180, "AppIcon60x60@3x.png")):
    bg.resize((size, size), Image.LANCZOS).save(os.path.join(out, name))
print("icones gerados em", out)
