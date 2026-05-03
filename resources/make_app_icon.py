#!/usr/bin/env python3
"""Render Stylus app icons.

Two outputs are written into resources/:

* `app-icon.png` (macOS): silver squircle on a transparent 1024x1024 canvas
  with a drop shadow and a soft top highlight, sized for the macOS Big Sur+
  app-icon template.
* `app-icon-ios.png` (iOS): full-bleed silver square, same play triangle.
  No transparent corners and no baked shadow (iOS applies its own squircle
  mask and renders shadows itself), so the icon ends up cleanly inset in the
  iOS home-screen squircle instead of double-rounded.

Re-run with `python3 resources/make_app_icon.py`.
"""

from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter, ImageChops

HERE       = Path(__file__).resolve().parent
OUT_MACOS  = HERE / "app-icon.png"
OUT_IOS    = HERE / "app-icon-ios.png"

# ---- Shared layout / palette -------------------------------------------------
W = H = 1024

# macOS Big Sur+ template: 824x824 content area inside the 1024x1024 canvas,
# 100 px transparent inset on each side that carries the drop shadow + glow.
MACOS_GRID_SIZE     = 824
MACOS_INSET         = (W - MACOS_GRID_SIZE) // 2   # 100
MACOS_CORNER_RADIUS = 185

BG_SILVER = (196, 196, 196)   # 0xc4c4c4 - transport-button silver
FG_BLACK  = ( 15,  15,  15)   # Stylus black

OPTICAL_NUDGE_RIGHT = 32      # play-triangle centre offset (same on both icons)


def render_play_triangle(height_ratio: float) -> Image.Image:
    """RGBA layer with a centred play triangle, optically nudged right and
    softened at the corners. Caller handles any squircle-shaped clipping."""
    layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    draw  = ImageDraw.Draw(layer)

    tri_h = int(W * height_ratio)
    tri_w = int(tri_h * 0.866)            # ~equilateral
    cx    = W // 2 + OPTICAL_NUDGE_RIGHT
    cy    = H // 2

    draw.polygon([
        (cx - tri_w // 2, cy - tri_h // 2),
        (cx - tri_w // 2, cy + tri_h // 2),
        (cx + tri_w // 2, cy),
    ], fill=FG_BLACK + (255,))

    return layer.filter(ImageFilter.GaussianBlur(radius=1.8))


def make_macos_icon() -> Image.Image:
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))

    # Squircle mask reused by triangle clipping + top highlight.
    mask = Image.new("L", (W, H), 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        (MACOS_INSET, MACOS_INSET, MACOS_INSET + MACOS_GRID_SIZE, MACOS_INSET + MACOS_GRID_SIZE),
        MACOS_CORNER_RADIUS,
        fill=255,
    )

    # Drop shadow: tight core + wide halo, both offset down ~24 px.
    halo = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(halo).rounded_rectangle(
        (MACOS_INSET, MACOS_INSET + 20, MACOS_INSET + MACOS_GRID_SIZE, MACOS_INSET + MACOS_GRID_SIZE + 20),
        MACOS_CORNER_RADIUS,
        fill=(0, 0, 0, 110),
    )
    halo = halo.filter(ImageFilter.GaussianBlur(radius=42))
    img  = Image.alpha_composite(img, halo)

    core = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(core).rounded_rectangle(
        (MACOS_INSET, MACOS_INSET + 28, MACOS_INSET + MACOS_GRID_SIZE, MACOS_INSET + MACOS_GRID_SIZE + 28),
        MACOS_CORNER_RADIUS,
        fill=(0, 0, 0, 200),
    )
    core = core.filter(ImageFilter.GaussianBlur(radius=20))
    img  = Image.alpha_composite(img, core)

    # Squircle body.
    body = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(body).rounded_rectangle(
        (MACOS_INSET, MACOS_INSET, MACOS_INSET + MACOS_GRID_SIZE, MACOS_INSET + MACOS_GRID_SIZE),
        MACOS_CORNER_RADIUS,
        fill=BG_SILVER + (255,),
    )
    img = Image.alpha_composite(img, body)

    # Play triangle, clipped to the squircle.
    tri = render_play_triangle(height_ratio=0.56 * MACOS_GRID_SIZE / W)
    tri.putalpha(ImageChops.multiply(tri.split()[3], mask))
    img = Image.alpha_composite(img, tri)

    # Top highlight: soft lit-from-above sheen.
    hl = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(hl).ellipse(
        (MACOS_INSET + 40, MACOS_INSET - 80, MACOS_INSET + MACOS_GRID_SIZE - 40, MACOS_INSET + 240),
        fill=(255, 255, 255, 45),
    )
    hl = hl.filter(ImageFilter.GaussianBlur(radius=36))
    hl.putalpha(ImageChops.multiply(hl.split()[3], mask))
    img = Image.alpha_composite(img, hl)

    return img


def make_ios_icon() -> Image.Image:
    # Opaque silver fills the full 1024x1024 - iOS's own squircle mask will
    # do the rounding when the icon is rendered on the home screen.
    img = Image.new("RGBA", (W, H), BG_SILVER + (255,))

    # Play triangle sized to look the same as the macOS variant once iOS
    # applies its squircle mask. macOS's triangle is 0.56 * 824 px (56 % of
    # the squircle width); on iOS the visible silver fills the full 1024 px
    # canvas at the centre line, so 0.56 of canvas keeps the same
    # triangle-to-visible-silver ratio.
    tri = render_play_triangle(height_ratio=0.56)
    img = Image.alpha_composite(img, tri)

    # Soft top highlight to match the macOS variant's lit-from-above feel.
    # No squircle mask: iOS clips the corners itself.
    hl = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(hl).ellipse(
        (40, -120, W - 40, 320),
        fill=(255, 255, 255, 45),
    )
    hl  = hl.filter(ImageFilter.GaussianBlur(radius=44))
    img = Image.alpha_composite(img, hl)

    return img


if __name__ == "__main__":
    make_macos_icon().save(OUT_MACOS, format="PNG")
    print(f"wrote {OUT_MACOS}")

    make_ios_icon().save(OUT_IOS, format="PNG")
    print(f"wrote {OUT_IOS}")
