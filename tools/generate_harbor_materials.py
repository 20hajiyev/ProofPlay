"""Baked procedural surface materials for Harbor (plan 3.3 material contract, 3.6 shared families).
Run: python tools/generate_harbor_materials.py

Every map is tileable (built from periodic FFT noise and np.roll gradients), 1024 x 1024:
  *_albedo.png  sRGB base colour
  *_normal.png  linear, tangent-space OpenGL (+Y green up), as glTF expects
  *_orm.png     linear, R = ambient occlusion, G = roughness, B = metallic
Original synthesis only: no photos, scans or downloaded textures. Deterministic (fixed seeds).
"""
import hashlib, json
from pathlib import Path
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'assets' / 'materials' / 'harbor'
N = 1024


def noise(seed, beta=2.0, lo=1.0, hi=None):
    """Periodic 1/f^beta noise in [-1, 1]; tiles seamlessly because it is built in frequency space."""
    rng = np.random.default_rng(seed)
    f = np.fft.fftfreq(N) * N
    fx, fy = np.meshgrid(f, f)
    r = np.sqrt(fx * fx + fy * fy)
    amp = np.where(r >= lo, 1.0 / np.maximum(r, 1.0) ** (beta / 2), 0.0)
    if hi is not None:
        amp *= r <= hi
    spec = (rng.standard_normal((N, N)) + 1j * rng.standard_normal((N, N))) * amp
    x = np.real(np.fft.ifft2(spec))
    # Scale by spread, not by the single largest value: keeps fine detail visible at any band.
    return np.clip(x / (2.5 * np.std(x) + 1e-12), -1, 1)


def normal_from_height(h, strength):
    dx = (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) * 0.5 * strength
    dy = (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) * 0.5 * strength
    # Image rows run downward; OpenGL convention wants +Y (green) pointing up the texture.
    n = np.stack([-dx, dy, np.ones_like(h)], axis=-1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return n * 0.5 + 0.5


def cavity_ao(h, depth=1.0):
    blur = h.copy()
    for s in (2, 4, 8):
        blur = (blur + np.roll(blur, s, 0) + np.roll(blur, -s, 0) + np.roll(blur, s, 1) + np.roll(blur, -s, 1)) / 5
    return np.clip(1.0 - np.maximum(blur - h, 0) * depth, 0.5, 1.0)


def srgb(linear):
    linear = np.clip(linear, 0, 1)
    return np.where(linear <= 0.0031308, linear * 12.92, 1.055 * linear ** (1 / 2.4) - 0.055)


def save(name, albedo_linear, normal, ao, rough, metal):
    OUT.mkdir(parents=True, exist_ok=True)
    files = {}
    maps = {
        'albedo': srgb(albedo_linear),
        'normal': normal,
        'orm': np.stack([ao, np.clip(rough, 0.03, 1), np.clip(metal, 0, 1)], axis=-1),
    }
    for kind, img in maps.items():
        p = OUT / f'{name}_{kind}.png'
        Image.fromarray((np.clip(img, 0, 1) * 255 + 0.5).astype(np.uint8), 'RGB').save(p, optimize=True)
        files[kind] = p
    return files


def grooves(period_px, width_px):
    """Periodic distance-to-line mask along both axes (slab joints)."""
    i = np.arange(N)
    d = np.minimum(i % period_px, period_px - i % period_px)
    g = np.clip(1 - d / width_px, 0, 1)
    return np.maximum(g[None, :], g[:, None])


def asphalt():
    fine = noise(11, 0.4, lo=120)                 # aggregate grain
    stones = np.clip(noise(12, 0.2, lo=200) * 3 - 1.6, 0, 1)
    patches = noise(13, 3.2, lo=1, hi=12)          # repairs, oil, wear
    h = 0.6 * fine + 0.8 * stones
    tone = 0.055 + 0.018 * fine + 0.05 * stones - 0.012 * np.clip(patches, 0, 1)
    albedo = np.stack([tone * 0.96, tone, tone * 1.06], -1)
    rough = 0.86 - 0.12 * stones - 0.08 * np.clip(-patches, 0, 1)
    return save('asphalt', albedo, normal_from_height(h, 6.0), cavity_ao(h, 0.8), rough, np.zeros_like(h))


def concrete_slab():
    # One texture = one 6 m quay slab; joints on the tile edges line up across the whole quay.
    pores = np.clip(noise(21, 0.3, lo=150) * 2.5 - 1.2, 0, 1)
    broad = noise(22, 3.0, lo=1, hi=16)
    stains = np.clip(noise(23, 2.6, lo=2, hi=40) * 1.8 - 0.4, 0, 1)
    joint = grooves(N, 5)
    h = -0.6 * pores - 2.0 * joint + 0.15 * broad
    tone = 0.30 + 0.035 * broad - 0.06 * stains - 0.08 * pores - 0.12 * joint
    albedo = np.stack([tone * 1.01, tone, tone * 0.95], -1)
    rough = 0.9 - 0.08 * stains
    return save('concrete_slab', albedo, normal_from_height(h, 4.0), cavity_ao(h, 0.6), rough, np.zeros_like(h))


def barrier_concrete():
    pores = np.clip(noise(31, 0.3, lo=160) * 2.5 - 1.3, 0, 1)
    formlines = np.clip(np.sin(np.arange(N) / N * 2 * np.pi * 4)[None, :] * 4 - 3.6, 0, 1) * 0.5
    grime = np.clip(noise(32, 2.8, lo=1, hi=24), 0, 1)
    h = -0.5 * pores - 0.4 * formlines
    tone = 0.46 - 0.07 * grime - 0.07 * pores - 0.04 * formlines
    albedo = np.stack([tone, tone * 0.99, tone * 0.95], -1)
    return save('barrier_concrete', albedo, normal_from_height(h, 3.0), cavity_ao(h, 0.5), 0.85 - 0.05 * grime, np.zeros_like(h))


PAINTS = {  # linear base colours; muted, weathered industrial paint
    'teal': (0.025, 0.20, 0.23), 'red': (0.30, 0.045, 0.03), 'blue': (0.025, 0.07, 0.22),
    'orange': (0.62, 0.17, 0.03), 'grey': (0.20, 0.21, 0.21),
}


def painted_metal():
    chips = np.clip(noise(41, 1.2, lo=20) * 2.2 - 1.45, 0, 1)            # scratches / chipped paint
    rust = np.clip(noise(42, 2.4, lo=2, hi=60) * 2.0 - 0.9, 0, 1) * np.clip(chips * 3, 0, 1)
    fade = noise(43, 3.0, lo=1, hi=10)
    h = -0.8 * chips + 0.1 * fade
    rust_col = np.array([0.20, 0.07, 0.025])
    bare_col = np.array([0.30, 0.31, 0.32])
    out = {}
    for name, col in PAINTS.items():
        base = np.array(col)[None, None, :] * (1 + 0.12 * fade[..., None])
        a = base * (1 - chips[..., None]) + bare_col * chips[..., None]
        a = a * (1 - rust[..., None]) + rust_col * rust[..., None]
        rough = 0.55 + 0.35 * rust + 0.1 * chips
        metal = chips * (1 - rust) * 0.9
        out[name] = save(f'paint_{name}', a, normal_from_height(h, 3.0), cavity_ao(h, 0.4), rough, metal)
    return out


def galvanized():
    spangle = noise(51, 0.8, lo=30)
    dull = noise(52, 3.0, lo=1, hi=12)
    tone = 0.52 + 0.06 * spangle + 0.04 * dull
    albedo = np.stack([tone * 0.97, tone, tone * 1.02], -1)
    h = 0.2 * spangle
    return save('galvanized_steel', albedo, normal_from_height(h, 0.6), np.ones((N, N)), 0.38 + 0.1 * spangle + 0.08 * dull, np.full((N, N), 0.85))


def water():
    h = 0.6 * noise(61, 2.2, lo=6, hi=90) + 0.4 * noise(62, 1.6, lo=40)
    albedo = np.stack([np.full((N, N), 0.012), np.full((N, N), 0.07), np.full((N, N), 0.09)], -1)
    return save('water', albedo, normal_from_height(h, 3.0), np.ones((N, N)), np.full((N, N), 0.08), np.zeros((N, N)))


def seam_error(p):
    """Tileability check: jump across the wrap edge vs typical neighbour differences."""
    x = np.asarray(Image.open(p), dtype=np.float64) / 255
    # Per column-pair mean jump; the wrap pair must look like an ordinary interior pair (a joint
    # groove drawn on the tile edge is as sharp as the same groove anywhere else).
    inner = np.mean(np.abs(np.diff(x, axis=1)), axis=(0, 2) if x.ndim == 3 else 0)
    wrap = np.mean(np.abs(x[:, 0] - x[:, -1]))
    return wrap / (np.max(inner) + 1e-9)


made = {'asphalt': asphalt(), 'concrete_slab': concrete_slab(), 'barrier_concrete': barrier_concrete(),
        'galvanized_steel': galvanized(), 'water': water()}
made.update({f'paint_{k}': v for k, v in painted_metal().items()})
records, bad = [], []
for mat, files in made.items():
    for kind, p in files.items():
        ratio = seam_error(p)
        if ratio >= 1.5:
            bad.append((p.name, ratio))
        records.append({'file': str(p.relative_to(ROOT)).replace('\\', '/'), 'material': mat, 'map': kind,
                        'color_space': 'sRGB' if kind == 'albedo' else 'linear', 'size': [N, N],
                        'seam_ratio': round(ratio, 3), 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()})
(OUT / 'manifest_fragment.json').write_text(json.dumps({
    'schema_version': 1, 'generator': 'tools/generate_harbor_materials.py',
    'provenance': 'Procedural synthesis (numpy FFT noise). No photographs, scans or downloaded textures.',
    'contract': 'albedo sRGB; normal tangent-space OpenGL; ORM R=AO G=roughness B=metallic (plan 3.3)',
    'maps': records}, indent=2), encoding='utf-8')
print('MATERIALS_COMPLETE', len(records), 'maps,', len(bad), 'seam failures')
for b in bad:
    print('SEAM', b)
