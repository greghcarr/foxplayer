#!/usr/bin/env python3
"""Render resources/app-icon.png: play-button triangle on silver squircle.

Same language as the transport play button (silver 0xc4c4c4 circle, black
triangle) but squircle-shaped to fit the macOS Big Sur+ app-icon template.
1024x1024 canvas, 824x824 content area, transparent padding carrying the
drop shadow and a subtle top highlight.

Re-run with `python3 resources/make_app_icon.py`.
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
BG_SQUIRCLE = (196, 196, 196)   # 0xc4c4c4 - transport-button silver
FG          = ( 15,  15,  15)   # Stylus black

# ---- Canvas ------------------------------------------------------------------
img = Image.new("RGBA", (W, H), (0, 0, 0, 0))

# Squircle mask (reused several times)
mask = Image.new("L", (W, H), 0)
ImageDraw.Draw(mask).rounded_rectangle(
    (INSET, INSET, INSET + GRID_SIZE, INSET + GRID_SIZE),
    CORNER_RADIUS,
    fill=255,
)

# ---- Drop shadow beneath the squircle (two passes: tight core + wide halo) ---
shadow_halo = Image.new("RGBA", (W, H), (0, 0, 0, 0))
ImageDraw.Draw(shadow_halo).rounded_rectangle(
    (INSET, INSET + 20, INSET + GRID_SIZE, INSET + GRID_SIZE + 20),
    CORNER_RADIUS,
    fill=(0, 0, 0, 110),
)
shadow_halo = shadow_halo.filter(ImageFilter.GaussianBlur(radius=42))
img = Image.alpha_composite(img, shadow_halo)

shadow_core = Image.new("RGBA", (W, H), (0, 0, 0, 0))
ImageDraw.Draw(shadow_core).rounded_rectangle(
    (INSET, INSET + 28, INSET + GRID_SIZE, INSET + GRID_SIZE + 28),
    CORNER_RADIUS,
    fill=(0, 0, 0, 200),
)
shadow_core = shadow_core.filter(ImageFilter.GaussianBlur(radius=20))
img = Image.alpha_composite(img, shadow_core)

# ---- Squircle body -----------------------------------------------------------
body = Image.new("RGBA", (W, H), (0, 0, 0, 0))
ImageDraw.Draw(body).rounded_rectangle(
    (INSET, INSET, INSET + GRID_SIZE, INSET + GRID_SIZE),
    CORNER_RADIUS,
    fill=BG_SQUIRCLE + (255,),
)
img = Image.alpha_composite(img, body)

# ---- Play triangle centered on the squircle ----------------------------------
# Equilateral-ish triangle pointing right, nudged slightly right of geometric
# centre for optical balance (same trick we use on the circular play button).
tri_layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
tdraw = ImageDraw.Draw(tri_layer)

TRI_H  = int(GRID_SIZE * 0.56)         # height
TRI_W  = int(TRI_H * 0.866)            # ~equilateral
tri_cx = W // 2 + 32                   # optical nudge right
tri_cy = H // 2

tdraw.polygon([
    (tri_cx - TRI_W // 2, tri_cy - TRI_H // 2),   # top-left
    (tri_cx - TRI_W // 2, tri_cy + TRI_H // 2),   # bottom-left
    (tri_cx + TRI_W // 2, tri_cy),                # point
], fill=FG + (255,))

# Soften the three corners slightly so they don't feel razor-sharp.
tri_layer = tri_layer.filter(ImageFilter.GaussianBlur(radius=1.8))
tri_layer.putalpha(ImageChops.multiply(tri_layer.split()[3], mask))
img = Image.alpha_composite(img, tri_layer)

# ---- Top highlight: soft lit-from-above sheen --------------------------------
hl = Image.new("RGBA", (W, H), (0, 0, 0, 0))
ImageDraw.Draw(hl).ellipse(
    (INSET + 40, INSET - 80, INSET + GRID_SIZE - 40, INSET + 240),
    fill=(255, 255, 255, 45),
)
hl = hl.filter(ImageFilter.GaussianBlur(radius=36))
hl.putalpha(ImageChops.multiply(hl.split()[3], mask))
img = Image.alpha_composite(img, hl)

img.save(OUT, format="PNG")
print(f"wrote {OUT}")
