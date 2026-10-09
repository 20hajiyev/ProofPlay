"""Prepares the CC0 Poly Haven scans in assets/materials/polyhaven/ for the game.
Run: python tools/prepare_scanned_materials.py

- Container paint variants: the scan's own detail (dents, streaks, corrugation shading) is kept
  as luminance and re-coloured to the Harbor paint palette, so one scan gives five containers.
- HDRI sun: finds the brightest region of the equirectangular sky so the game's directional
  light (shadows) matches the sun visible in the sky.
Outputs go to assets/materials/scanned/ (derived works of CC0 sources, also CC0).
"""
import json, math
from pathlib import Path
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'assets' / 'materials' / 'polyhaven'
OUT = ROOT / 'assets' / 'materials' / 'scanned'
OUT.mkdir(parents=True, exist_ok=True)

PAINTS = {  # sRGB target mean colours, weathered industrial paint
    'teal': (38, 104, 110), 'red': (128, 44, 34), 'blue': (38, 62, 112), 'orange': (176, 92, 30), 'grey': (112, 116, 116),
}


def container_variants():
    src = np.asarray(Image.open(SRC / 'container_side_diff_2k.jpg').convert('RGB'), dtype=np.float64)
    lum = src.mean(axis=2)
    detail = lum / lum.mean()  # 1.0 = average paint, darker streaks/dents < 1
    for name, rgb in PAINTS.items():
        out = np.clip(detail[..., None] * np.array(rgb)[None, None, :], 0, 255).astype(np.uint8)
        Image.fromarray(out, 'RGB').resize((1024, 1024), Image.LANCZOS).save(OUT / f'container_{name}_diff.jpg', quality=92)
    for kind in ('nor_gl', 'arm'):
        Image.open(SRC / f'container_side_{kind}_2k.jpg').resize((1024, 1024), Image.LANCZOS).save(OUT / f'container_{kind}.jpg', quality=95)


def read_hdr(path):
    """Minimal Radiance RGBE reader (new-style RLE), enough to locate the sun."""
    data = path.read_bytes()
    header_end = data.index(b'\n\n') + 2
    line_end = data.index(b'\n', header_end)
    dims = data[header_end:line_end].split()
    h, w = int(dims[1]), int(dims[3])
    pos = line_end + 1
    img = np.zeros((h, w, 4), dtype=np.uint8)
    for y in range(h):
        pos += 4  # scanline header 2 2 hi lo
        for c in range(4):
            x = 0
            while x < w:
                n = data[pos]; pos += 1
                if n > 128:
                    n -= 128; img[y, x:x + n, c] = data[pos]; pos += 1
                else:
                    img[y, x:x + n, c] = np.frombuffer(data[pos:pos + n], dtype=np.uint8); pos += n
                x += n
    e = img[..., 3].astype(np.int32)
    scale = np.where(e > 0, np.ldexp(1.0, e - 136), 0.0)
    return img[..., :3] * scale[..., None]


def hdri_sun():
    rgb = read_hdr(SRC / 'industrial_sunset_02_puresky_2k.hdr')
    lum = rgb @ np.array([0.2126, 0.7152, 0.0722])
    h, w = lum.shape
    y, x = np.unravel_index(np.argmax(lum), lum.shape)
    # Equirectangular: u = 0.5 is straight ahead (-Z in most engines), v = 0 is the zenith.
    elevation = 90.0 - (y + 0.5) / h * 180.0
    azimuth = ((x + 0.5) / w - 0.5) * 360.0
    return {'sun_elevation_deg': round(elevation, 2), 'sun_azimuth_deg': round(azimuth, 2),
            'peak_luminance': round(float(lum[y, x]), 1), 'pixel': [int(x), int(y)], 'size': [w, h]}



def graded(src_name, out_name, keep_saturation, target_mean):
    """Colour-grades a scan to the Harbor palette: mostly neutral grey at a measured brightness.
    Poly Haven's asphalt_01 / concrete_floor_02 are warm brown; in game they read as dirt."""
    x = np.asarray(Image.open(SRC / src_name).convert('RGB'), dtype=np.float64)
    grey = x.mean(axis=2, keepdims=True)
    x = grey + (x - grey) * keep_saturation
    x *= target_mean / x.mean()
    Image.fromarray(np.clip(x, 0, 255).astype(np.uint8), 'RGB').save(OUT / out_name, quality=92)


graded('asphalt_01_diff_2k.jpg', 'asphalt_graded_diff.jpg', 0.15, 72.0)
graded('concrete_floor_02_diff_2k.jpg', 'concrete_graded_diff.jpg', 0.35, 118.0)
container_variants()
sun = hdri_sun()
(OUT / 'hdri_sun.json').write_text(json.dumps(sun, indent=2), encoding='utf-8')
(ROOT / 'assets' / 'materials' / 'polyhaven' / 'LICENSE.md').write_text(
    '# Poly Haven assets (CC0 1.0, public domain)\n\n'
    'Downloaded 2026-09-28 from https://polyhaven.com (license: https://polyhaven.com/license), unmodified:\n\n'
    '- asphalt_01, concrete_floor_02, container_side, rusty_metal_02 - 2K JPG diff / nor_gl / arm\n'
    '- industrial_sunset_02_puresky - 2K HDR\n\n'
    'No attribution required; credited anyway in the game credits (plan 2.x "Credits və lisenziyalar").\n'
    'Derived files (tinted containers, resized maps) are in assets/materials/scanned/.\n', encoding='utf-8')
print('SCANNED_READY', json.dumps(sun))
