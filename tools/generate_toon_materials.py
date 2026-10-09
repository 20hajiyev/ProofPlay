"""Hand-painted toon surface textures for the environment (D-077). Run: python tools/generate_toon_materials.py

Owner (2026-10-02): "add detail to the environment - roads, walls... not new objects: instead of a
flat, very plain look, a more complex style that is pleasing to the eye". The surfaces stay the
same objects; they get comic-book texture: a few posterized tones (cel shading reads them as
painted), ink cracks and seams, grime and wear. Every map tiles seamlessly (periodic noise and
wrapped Voronoi), so the existing world-space UVs repeat without a seam.

Output: assets/materials/toon/<name>.png (sRGB albedo, 1024 px).
"""
from pathlib import Path

import numpy as np
from PIL import Image
from scipy.spatial import cKDTree

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'assets' / 'materials' / 'toon'
N = 1024


def periodic_noise(rng, scale_px, n=N):
    """Smooth noise that wraps: white noise low-passed in the frequency domain, 0..1."""
    spec = np.fft.rfft2(rng.standard_normal((n, n)))
    fy = np.fft.fftfreq(n)[:, None]
    fx = np.fft.rfftfreq(n)[None, :]
    f = np.sqrt(fx * fx + fy * fy)
    spec *= np.exp(-(f * scale_px) ** 2)
    x = np.fft.irfft2(spec, (n, n))
    x -= x.min()
    return x / max(x.max(), 1e-9)


def fbm(rng, scales):
    out = np.zeros((N, N))
    w = 1.0
    for s in scales:
        out += periodic_noise(rng, s) * w
        w *= 0.5
    out -= out.min()
    return out / out.max()


def posterize(x, levels):
    return np.floor(np.clip(x, 0, 0.9999) * levels) / (levels - 1)


def voronoi_edges(rng, cells, n=N):
    """Distance to the nearest Voronoi edge (F2 - F1) on a wrapped grid of jittered points, in px."""
    step = n / cells
    pts = []
    for i in range(cells):
        for j in range(cells):
            pts.append(((i + rng.uniform(0.15, 0.85)) * step, (j + rng.uniform(0.15, 0.85)) * step))
    pts = np.array(pts)
    tiled = np.concatenate([pts + np.array([dx, dy]) * n for dx in (-1, 0, 1) for dy in (-1, 0, 1)])
    yy, xx = np.mgrid[0:n, 0:n]
    d, _ = cKDTree(tiled).query(np.stack([xx.ravel(), yy.ravel()], axis=1), k=2)
    return (d[:, 1] - d[:, 0]).reshape(n, n)


def rgb(base, shade):
    """base colour scaled by a 0..1 tone map (0.5 = the base)."""
    b = np.array(base, np.float32)
    return np.clip(b[None, None, :] * (0.55 + 0.9 * shade[..., None]), 0, 1)


def ink(img, mask, colour, strength=1.0):
    c = np.array(colour, np.float32)
    m = np.clip(mask, 0, 1)[..., None] * strength
    return img * (1 - m) + c[None, None, :] * m


def save(name, img):
    OUT.mkdir(parents=True, exist_ok=True)
    Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8)).save(OUT / f'{name}.png', optimize=True)
    print('wrote', name)


def asphalt():
    """Road, 6 m per repeat: blue-grey asphalt in three posterized tones, light aggregate speckle,
    ink crack networks in patches, darker oil blotches."""
    rng = np.random.default_rng(101)
    # mid blue-grey (a near-black road hid every detail under the cel shading and the fog)
    tone = posterize(0.44 + 0.11 * fbm(rng, [90, 40, 14]), 3)
    img = rgb((0.25, 0.26, 0.3), tone)
    speck = periodic_noise(rng, 1.5)
    hi, lo = np.percentile(speck, 96.5), np.percentile(speck, 3.0)
    img = ink(img, (speck > hi).astype(np.float32), (0.42, 0.44, 0.48), 0.8)   # aggregate
    img = ink(img, (speck < lo).astype(np.float32), (0.13, 0.135, 0.16), 0.7)
    oil = periodic_noise(rng, 55)
    img = ink(img, (oil > np.percentile(oil, 94)).astype(np.float32), (0.17, 0.175, 0.2), 0.6)
    edges = voronoi_edges(rng, 7)
    cz = periodic_noise(rng, 120)
    crack_zone = cz > np.percentile(cz, 55)
    wobble = periodic_noise(rng, 6) * 3.0
    cracks = ((edges + wobble) < 5.5) & crack_zone
    img = ink(img, cracks.astype(np.float32), (0.03, 0.032, 0.045), 0.95)
    # tar-sealed seams running along the road (texture v = distance along the track)
    yy, xx = np.mgrid[0:N, 0:N]
    for k, x0 in enumerate((0.23, 0.71)):
        drift = periodic_noise(rng, 80)[:, 0] * 60 - 30
        cx = x0 * N + drift[:, None]
        seam = np.abs(xx - cx) < (7 + 3 * k)
        img = ink(img, seam.astype(np.float32), (0.1, 0.105, 0.125), 0.85)
    save('toon_asphalt', img)


def barrier():
    """Concrete barrier, 2 m per repeat (u along, v up): a vertical panel seam at the tile edge, a
    chamfer line near the top, grime rising from the bottom, chips and posterized mottling."""
    rng = np.random.default_rng(202)
    tone = posterize(0.45 + 0.25 * fbm(rng, [70, 25]), 4)
    img = rgb((0.72, 0.73, 0.76), tone)
    v = np.linspace(0, 1, N)[:, None] * np.ones((1, N))          # image row 0 = top of the texture
    up = 1.0 - v                                                 # 0 at the bottom row... (v=z/2 m)
    grime = np.clip((0.22 - up) / 0.22, 0, 1) * (0.55 + 0.45 * periodic_noise(rng, 30))
    img = ink(img, posterize(grime, 3), (0.32, 0.31, 0.3), 0.75)
    sx = periodic_noise(rng, 14)[:1, :] * np.ones((N, 1))       # vertical rain streaks: constant down a column
    streaks = (sx > np.percentile(sx, 93)) & (up > 0.22 + 0.12 * periodic_noise(rng, 60)) & (up < 0.495)
    img = ink(img, streaks.astype(np.float32), (0.5, 0.5, 0.52), 0.45)
    chamfer = (up > 0.5) & (up < 0.515)                          # the barrier's top edge (1.03-1.07 m of 2 m)
    img = ink(img, chamfer.astype(np.float32), (0.25, 0.25, 0.28), 0.8)
    img = ink(img, ((up > 0.515) & (up < 0.535)).astype(np.float32), (0.92, 0.92, 0.94), 0.6)
    seam = np.zeros((N, N), np.float32)
    seam[:, :6] = 1
    seam[:, -6:] = 1
    img = ink(img, seam, (0.12, 0.12, 0.14), 0.9)
    chips = voronoi_edges(rng, 16) > 26
    img = ink(img, (chips & (periodic_noise(rng, 10) > 0.8)).astype(np.float32), (0.5, 0.5, 0.53), 0.6)
    save('toon_barrier', img)


def quay(n=2048):
    """Quay ground, 16 m per repeat (D-086, owner: "the land beside the roads is only flat beige"): 8 x 8
    concrete slabs of 2 m with ink seams, each slab its own tone; two darker resurfaced asphalt
    patches; painted yard markings - yellow lane lines, a hatched no-park box, white bay ticks; drain
    grates; tyre marks; rust and oil stains; large posterized tone drift so 16 m repeats do not read."""
    rng = np.random.default_rng(303)
    yy, xx = np.mgrid[0:n, 0:n]
    cell = n // 8
    tone = 0.47 + 0.07 * (_noise(rng, 220, n) - 0.5) * 2 + 0.05 * _noise(rng, 50, n)
    slab = (xx // cell) + 8 * (yy // cell)
    for k in range(64):
        tone[slab == k] += rng.uniform(-0.06, 0.06)
    img = rgb((0.5, 0.44, 0.35), posterize(tone, 6))
    # resurfaced patches: darker grey asphalt with their own cracked edge
    for cx, cy, w, h in ((0.22, 0.3, 0.3, 0.18), (0.66, 0.72, 0.22, 0.3)):
        patch = (np.abs(xx / n - cx) < w / 2) & (np.abs(yy / n - cy) < h / 2)
        img = ink(img, patch.astype(np.float32), (0.27, 0.27, 0.29), 0.85)
        rim = patch & ~((np.abs(xx / n - cx) < w / 2 - 0.004) & (np.abs(yy / n - cy) < h / 2 - 0.004))
        img = ink(img, rim.astype(np.float32), (0.12, 0.11, 0.1), 0.9)
    seam = ((xx % cell) < 6) | ((yy % cell) < 6)
    img = ink(img, seam.astype(np.float32), (0.22, 0.18, 0.13), 0.75)
    stain = _noise(rng, 60, n)
    img = ink(img, (stain > np.percentile(stain, 95)).astype(np.float32), (0.36, 0.25, 0.15), 0.4)
    oil = _noise(rng, 30, n)
    img = ink(img, (oil < np.percentile(oil, 2.5)).astype(np.float32), (0.13, 0.12, 0.12), 0.55)
    # yard markings: yellow lane lines along x, a hatched box, white bay ticks
    yellow = (0.95, 0.72, 0.1)
    for y0 in (0.12, 0.88):
        img = ink(img, (np.abs(yy / n - y0) < 0.0045).astype(np.float32), yellow, 0.9)
    box_ = (np.abs(xx / n - 0.4) < 0.07) & (np.abs(yy / n - 0.55) < 0.05)
    edge = box_ & ~((np.abs(xx / n - 0.4) < 0.066) & (np.abs(yy / n - 0.55) < 0.046))
    hatch = box_ & (((xx + yy) % 48) < 10)
    img = ink(img, (edge | hatch).astype(np.float32), yellow, 0.9)
    for k in range(10):
        tick = (np.abs(xx / n - (0.05 + k * 0.1)) < 0.003) & (np.abs(yy / n - 0.16) < 0.04)
        img = ink(img, tick.astype(np.float32), (0.92, 0.92, 0.88), 0.85)
    # drain grates: dark rectangles with ink slots
    for gx, gy in ((0.25, 0.75), (0.8, 0.2), (0.55, 0.95)):
        g = (np.abs(xx / n - gx) < 0.012) & (np.abs(yy / n - gy) < 0.02)
        img = ink(img, g.astype(np.float32), (0.18, 0.18, 0.2), 0.95)
        img = ink(img, (g & ((yy % 10) < 3)).astype(np.float32), (0.05, 0.05, 0.06), 0.95)
    # tyre marks: two long faint parallel curves (forklifts and trucks)
    for off in (0.0, 0.012):
        curve = np.abs(yy / n - (0.38 + off + 0.05 * np.sin(xx / n * 6.2832 * 2))) < 0.0025
        img = ink(img, curve.astype(np.float32), (0.2, 0.18, 0.15), 0.45)
    cracks = ((voronoi_edges(rng, 6, n) + _noise(rng, 5, n) * 2.5) < 2.0) & (_noise(rng, 150, n) > 0.65)
    img = ink(img, cracks.astype(np.float32), (0.14, 0.11, 0.08), 0.85)
    save('toon_quay', img)


def apron(n=1024):
    """Worn asphalt apron outside the barriers, 6 m per repeat: lighter, sun-bleached, gravel at the
    outer edge, weeds' dark specks along the barrier side (u across, v along)."""
    rng = np.random.default_rng(505)
    tone = posterize(0.5 + 0.12 * fbm(rng, [80, 30, 10]), 4)
    img = rgb((0.36, 0.35, 0.34), tone)
    speck = _noise(rng, 1.5, n)
    img = ink(img, (speck > np.percentile(speck, 96)).astype(np.float32), (0.55, 0.53, 0.5), 0.7)
    xx = np.mgrid[0:n, 0:n][1] / n
    gravel = (xx > 0.82) & (_noise(rng, 3, n) > 0.45)
    img = ink(img, gravel.astype(np.float32), (0.52, 0.46, 0.37), 0.85)
    weeds = (xx < 0.08) & (_noise(rng, 2, n) > 0.8)
    img = ink(img, weeds.astype(np.float32), (0.2, 0.3, 0.14), 0.85)
    cracks = ((voronoi_edges(rng, 5, n) + _noise(rng, 6, n) * 3) < 4) & (_noise(rng, 100, n) > 0.5)
    img = ink(img, cracks.astype(np.float32), (0.12, 0.12, 0.13), 0.9)
    save('toon_apron', img)


def water():
    """Harbour water, 24 m per repeat: two-tone ripple bands and white glint dashes."""
    rng = np.random.default_rng(404)
    yy, xx = np.mgrid[0:N, 0:N] / N
    warp = periodic_noise(rng, 110)
    wave = np.sin(2 * np.pi * (yy * 5 + warp * 1.2))
    tone = np.where(wave > 0.55, 0.62, np.where(wave < -0.6, 0.36, 0.48))
    img = rgb((0.1, 0.5, 0.65), tone)
    gl = periodic_noise(rng, 12)
    glint = (np.sin(2 * np.pi * (yy * 15 + warp * 2)) > 0.96) & (gl > np.percentile(gl, 70))
    img = ink(img, glint.astype(np.float32), (0.85, 0.95, 1.0), 0.85)
    save('toon_water', img)


def _noise(rng, scale_px, n):
    """periodic_noise at another resolution (the paint maps are 512 px: many instances, small texel need)."""
    return periodic_noise(rng, scale_px * n / N, n)


def weathered_paint(name, base, seed, n=512, rust=1.0):
    """Industrial painted steel (D-082), one repeat = the material's TILE (1.9-2.5 m): posterized
    fading, vertical rain streaks, rust bleeding down from chips, scuffs - the containers', sheds' and
    cranes' paint gets a used, comic-book finish instead of one flat colour. u along, v up."""
    rng = np.random.default_rng(seed)
    fade = _noise(rng, 70, n) * 0.6 + _noise(rng, 22, n) * 0.4
    img = rgb(base, posterize(0.45 + 0.1 * fade, 3))
    # rain streaks: lighter washed columns and darker dirt columns, broken along their length
    col = _noise(rng, 6, n)[:1, :] * np.ones((n, 1))
    brk = _noise(rng, 40, n)
    img = ink(img, ((col > np.percentile(col, 96)) & (brk > 0.5)).astype(np.float32), tuple(min(1, c * 1.25 + 0.04) for c in base), 0.5)
    img = ink(img, ((col < np.percentile(col, 3)) & (brk > 0.55)).astype(np.float32), tuple(c * 0.6 for c in base), 0.55)
    # paint chips: small cells with a dark ink rim, bare primer/rust inside, a rust run below each
    cells = voronoi_edges(rng, 9, n)
    sel = _noise(rng, 5, n)
    chip_core = (cells > 0.42 * n / 9) & (sel > np.percentile(sel, 100 - 2.5 * rust))
    rim = (cells > 0.36 * n / 9) & (sel > np.percentile(sel, 100 - 2.5 * rust)) & ~chip_core
    run = np.zeros_like(chip_core)
    for k in range(1, int(0.05 * n)):  # rust bleeds downwards (image rows grow downwards); rolled: tiles
        run |= np.roll(chip_core, k, axis=0)
    run &= ~chip_core
    taper = _noise(rng, 4, n) > 0.45
    img = ink(img, (run & taper).astype(np.float32), (0.5, 0.24, 0.1), 0.45 * rust)
    img = ink(img, rim.astype(np.float32), (0.08, 0.06, 0.05), 0.85)
    img = ink(img, chip_core.astype(np.float32), (0.55, 0.28, 0.12), 0.9)
    # scuffs: short horizontal scratches in a lighter tone
    sc = _noise(rng, 2, n)
    yy = np.arange(n)[:, None] * np.ones((1, n))
    scratch = (sc > np.percentile(sc, 99.3)) & ((yy % 7) < 1)
    img = ink(img, scratch.astype(np.float32), (0.85, 0.85, 0.82), 0.5)
    save(name, img)


def crane_steel(n=1024):
    """The cranes' painted box girders (D-082), 2 m per repeat: orange plate with a welded seam and a
    rivet row at each plate edge, rust weeping under the rivets, posterized fading, a few ink
    hatch marks in the lower third (the comic way of drawing grime)."""
    rng = np.random.default_rng(606)
    base = (0.9, 0.45, 0.12)
    fade = _noise(rng, 90, n) * 0.6 + _noise(rng, 25, n) * 0.4
    img = rgb(base, posterize(0.45 + 0.18 * fade, 4))
    yy, xx = np.mgrid[0:n, 0:n]
    seam = (xx < 4) | (xx > n - 5) | (yy < 4) | (yy > n - 5)
    img = ink(img, seam.astype(np.float32), (0.25, 0.1, 0.03), 0.9)
    rivets = np.zeros((n, n), bool)
    for c0 in (14, n - 15):
        for r in range(16, n, 42):
            rivets |= ((xx - c0) ** 2 + (yy - r) ** 2) < 25
            rivets |= ((yy - c0) ** 2 + (xx - r) ** 2) < 25
    weep = np.zeros((n, n), bool)
    w = _noise(rng, 30, n) > 0.55
    for k in range(6, 60):
        weep |= np.roll(rivets, k, axis=0)
    img = ink(img, (weep & w & ~rivets).astype(np.float32), (0.55, 0.22, 0.08), 0.45)
    img = ink(img, rivets.astype(np.float32), (0.3, 0.12, 0.04), 0.95)
    hatch_zone = (yy > 0.68 * n) & (_noise(rng, 50, n) > 0.55)
    hatch = ((xx + yy) % 22) < 3
    img = ink(img, (hatch_zone & hatch).astype(np.float32), (0.45, 0.2, 0.06), 0.6)
    cells = voronoi_edges(rng, 8, n)
    chips = (cells > 0.4 * n / 8) & (_noise(rng, 6, n) > 0.86)
    img = ink(img, chips.astype(np.float32), (0.45, 0.25, 0.14), 0.85)
    save('toon_crane', img)


def container_emblems(n=1024):
    """Painted emblem panels for container sides (D-082), a 2 x 2 atlas: a white panel with an ink
    border and one abstract shipping-line symbol each - wave, star, chevrons, diamond-and-ring. No
    names, no real company marks."""
    img = np.ones((n, n, 3), np.float32) * np.array((0.93, 0.92, 0.86), np.float32)
    h = n // 2
    yy, xx = np.mgrid[0:h, 0:h] / h  # 0..1 inside one cell; the cell is 2.4 x 1.0 m, so x is squeezed
    ax = (xx - 0.5) * 2.4
    ay = (yy - 0.5) * 1.0
    border = (np.minimum(np.minimum(xx, 1 - xx) * 2.4, np.minimum(yy, 1 - yy)) < 0.035)
    inner = (np.minimum(np.minimum(xx, 1 - xx) * 2.4, np.minimum(yy, 1 - yy)) < 0.06) & ~border
    navy, red, teal, gold = (0.08, 0.14, 0.38), (0.8, 0.12, 0.1), (0.05, 0.5, 0.52), (0.95, 0.66, 0.1)
    wave = (np.abs(ay - 0.16 * np.sin(ax * 5.5)) < 0.07) | (np.abs(ay - 0.16 * np.sin(ax * 5.5) - 0.22) < 0.045)
    r = np.sqrt(ax * ax + ay * ay)
    th = np.arctan2(ay, ax)
    star = r < 0.36 * (0.55 + 0.45 * np.abs(np.cos(2.5 * th)))
    chev = np.zeros_like(ax, bool)
    for k in range(3):
        cx = -0.45 + k * 0.45
        chev |= (np.abs(np.abs(ay) * 1.0 - (ax - cx) * -1.0 - 0.0) < 0.08) & (np.abs(ay) < 0.32) & (ax - cx > -0.35) & (ax - cx < 0.02)
    diamond = (np.abs(ax) + np.abs(ay) < 0.4) & ~(np.abs(ax) + np.abs(ay) < 0.28) | ((r < 0.12))
    cells = [(wave, navy), (star, red), (chev, teal), (diamond, gold)]
    for i, (shape, colour) in enumerate(cells):
        cell = np.ones((h, h, 3), np.float32) * np.array((0.93, 0.92, 0.86), np.float32)
        cell = ink(cell, shape.astype(np.float32), colour, 1.0)
        cell = ink(cell, inner.astype(np.float32), colour, 1.0)
        cell = ink(cell, border.astype(np.float32), (0.05, 0.05, 0.06), 1.0)
        r0, c0 = (i // 2) * h, (i % 2) * h
        img[r0:r0 + h, c0:c0 + h] = cell
    save('toon_emblems', img)


if __name__ == '__main__':
    asphalt()
    barrier()
    quay()
    apron()
    water()
    # D-082: weathered paint for containers, sheds and cranes, container emblem panels
    for colour, base, seed in (('teal', (0.08, 0.55, 0.55), 11), ('red', (0.85, 0.2, 0.15), 12), ('blue', (0.15, 0.35, 0.8), 13),
                               ('orange', (0.98, 0.55, 0.12), 14), ('grey', (0.6, 0.62, 0.66), 15)):
        weathered_paint('toon_box_' + colour, base, seed)
    for colour, base, seed in (('teal', (0.05, 0.42, 0.45), 21), ('grey', (0.45, 0.47, 0.5), 22), ('white', (0.96, 0.96, 0.92), 23)):
        weathered_paint('toon_clad_' + colour, base, seed, rust=0.6)
    crane_steel()
    container_emblems()
