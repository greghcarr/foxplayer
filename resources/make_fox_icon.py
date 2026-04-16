#!/usr/bin/env python3
"""Render resources/app-icon.png: wavy brown fox on a cream squircle.

Follows the macOS Big Sur+ app-icon template (1024x1024 canvas, 824x824
content area, transparent padding carrying the shadow + top highlight).
Re-run with `python3 resources/make_fox_icon.py`.
"""

from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter, ImageChops

HERE = Path(__file__).resolve().parent
OUT  = HERE / "app-icon.png"

# ---- Apple macOS app-icon template -------------------------------------------
W = H = 1024
GRID_SIZE     = 824
INSET         = (W - GRID_SIZE) // 2   # 100
CORNER_RADIUS = 185

# ---- Palette -----------------------------------------------------------------
BG_SQUIRCLE = (241, 232, 213)   # warm cream
FOX_BROWN   = (168,  88,  40)   # warm red-brown
FOX_DEEP    = (120,  58,  26)   # darker brown for shading / ear tips
FOX_CREAM   = (250, 240, 220)   # fox belly / muzzle
FOX_DARK    = ( 40,  25,  15)   # eyes / nose

# ---- Canvas ------------------------------------------------------------------
img = Image.new("RGBA", (W, H), (0, 0, 0, 0))

# Squircle mask (reused several times)
mask = Image.new("L", (W, H), 0)
ImageDraw.Draw(mask).rounded_rectangle(
    (INSET, INSET, INSET + GRID_SIZE, INSET + GRID_SIZE),
    CORNER_RADIUS,
    fill=255,
)

# ---- Drop shadow beneath the squircle ----------------------------------------
shadow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
ImageDraw.Draw(shadow).rounded_rectangle(
    (INSET, INSET + 24, INSET + GRID_SIZE, INSET + GRID_SIZE + 24),
    CORNER_RADIUS,
    fill=(0, 0, 0, 130),
)
shadow = shadow.filter(ImageFilter.GaussianBlur(radius=22))
img = Image.alpha_composite(img, shadow)

# ---- Squircle body -----------------------------------------------------------
body = Image.new("RGBA", (W, H), (0, 0, 0, 0))
ImageDraw.Draw(body).rounded_rectangle(
    (INSET, INSET, INSET + GRID_SIZE, INSET + GRID_SIZE),
    CORNER_RADIUS,
    fill=BG_SQUIRCLE + (255,),
)
img = Image.alpha_composite(img, body)

# ---- Fox (drawn on its own layer, masked to the squircle) --------------------
fox = Image.new("RGBA", (W, H), (0, 0, 0, 0))
d = ImageDraw.Draw(fox)

cx = W // 2
cy = 600  # slight optical centre below geometric centre so ears fit

def ell(x0, y0, x1, y1, color):
    d.ellipse((x0, y0, x1, y1), fill=color + (255,))

# ---- Ears (teardrop/ovals tilted inward via ellipse overlap) -----------------
# Outer ear shapes first (brown), then inner cream, then tiny dark tip hint.
ell(cx - 330, cy - 470, cx -  80, cy - 100, FOX_BROWN)   # left outer
ell(cx +  80, cy - 470, cx + 330, cy - 100, FOX_BROWN)
ell(cx - 290, cy - 430, cx - 120, cy - 140, FOX_CREAM)   # left inner
ell(cx + 120, cy - 430, cx + 290, cy - 140, FOX_CREAM)

# ---- Face base (big oval) ----------------------------------------------------
ell(cx - 310, cy - 230, cx + 310, cy + 280, FOX_BROWN)

# ---- Cheek puffs (overlap the face to soften and widen the lower half) ------
ell(cx - 380, cy -  40, cx -  50, cy + 320, FOX_BROWN)   # left cheek
ell(cx +  50, cy -  40, cx + 380, cy + 320, FOX_BROWN)   # right cheek

# ---- Chin / muzzle (wavy cream cloud under the eyes) -------------------------
# Overlapping cream ovals create an organic, wavy underbelly.
ell(cx - 230, cy +  70, cx + 230, cy + 360, FOX_CREAM)
ell(cx - 160, cy + 150, cx + 160, cy + 380, FOX_CREAM)

# ---- Forehead blaze (thin cream stripe between the eyes) ---------------------
ell(cx -  55, cy - 180, cx +  55, cy +  80, FOX_CREAM)

# ---- Eyes --------------------------------------------------------------------
ell(cx - 170, cy -  40, cx -  90, cy +  50, FOX_DARK)
ell(cx +  90, cy -  40, cx + 170, cy +  50, FOX_DARK)

# Tiny cream catch-lights
ell(cx - 140, cy -  25, cx - 115, cy +   0, FOX_CREAM)
ell(cx + 115, cy -  25, cx + 140, cy +   0, FOX_CREAM)

# ---- Nose --------------------------------------------------------------------
ell(cx -  55, cy + 200, cx +  55, cy + 260, FOX_DARK)

# ---- Snout shadow (subtle darker blush on the lower sides) -------------------
ell(cx - 290, cy + 140, cx - 140, cy + 300, FOX_DEEP)
ell(cx + 140, cy + 140, cx + 290, cy + 300, FOX_DEEP)

# Soften edges with a tiny blur so everything reads as "wavy".
fox = fox.filter(ImageFilter.GaussianBlur(radius=1.5))

# Mask to squircle shape
fox.putalpha(ImageChops.multiply(fox.split()[3], mask))
img = Image.alpha_composite(img, fox)

# ---- Top highlight: soft lit-from-above sheen --------------------------------
hl = Image.new("RGBA", (W, H), (0, 0, 0, 0))
ImageDraw.Draw(hl).ellipse(
    (INSET + 40, INSET - 80, INSET + GRID_SIZE - 40, INSET + 240),
    fill=(255, 255, 255, 40),
)
hl = hl.filter(ImageFilter.GaussianBlur(radius=36))
hl.putalpha(ImageChops.multiply(hl.split()[3], mask))
img = Image.alpha_composite(img, hl)

img.save(OUT, format="PNG")
print(f"wrote {OUT}")
