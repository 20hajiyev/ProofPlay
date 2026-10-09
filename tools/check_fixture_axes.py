"""Imports fixtures and vehicle LOD0s through Wicked and checks the axis contract (D-002).

Measured mapping for Wicked v0.72.106 glTF import: (x, y, z)_blender -> (x, z, y)_wicked.
Runtime contract: +Y up, vehicle front +Z, vehicle left -X.
Usage: python tools/check_fixture_axes.py <path-to-import_check.exe>
"""
import json, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOL = 1e-3
FAILURES = 0


def report(ok, label):
    global FAILURES
    FAILURES += not ok
    print(f"{'PASS' if ok else 'FAIL'} {label}")


def import_entities(exe, glb):
    out = subprocess.run([exe, str(glb)], capture_output=True, text=True, check=True).stdout
    return json.loads(out.replace('\\', '/'))['entities']


def check_fixtures(exe):
    manifest = json.loads((ROOT / 'assets/fixtures/manifest_fragment.json').read_text(encoding='utf8'))
    for asset in manifest['assets']:
        imported = import_entities(exe, ROOT / asset['runtime_paths'][0])
        for name, (x, y, z) in asset['blender_marker_positions_m'].items():
            expected, actual = [x, z, y], imported.get(name)
            ok = actual is not None and all(abs(a - e) <= TOL for a, e in zip(actual, expected))
            report(ok, f"{asset['asset_id']}.{name}: expected {expected}, got {actual}")
    veh = import_entities(exe, ROOT / 'assets/fixtures/FIXTURE_VEHICLE_AXES.glb')
    report(veh['VEH_NOSE_MARK'][2] > 0, 'fixture vehicle front faces +Z')
    report(veh['VEH_LEFT_MARK'][0] < 0, 'fixture vehicle left side is -X')


def check_vehicles(exe):
    # Front wheels ahead (+Z), left wheels on -X.
    expect = {'FL': (-1, 1), 'FR': (1, 1), 'RL': (-1, -1), 'RR': (1, -1)}
    for glb in sorted((ROOT / 'assets/vehicles').glob('*/*_LOD0.glb')):
        entities = import_entities(exe, glb)
        for wheel, (sx, sz) in expect.items():
            p = entities.get(f'WHEEL_PIVOT_{wheel}')
            ok = p is not None and p[0] * sx > 0 and p[2] * sz > 0
            report(ok, f'{glb.parent.name} WHEEL_PIVOT_{wheel} at {p}')


if __name__ == '__main__':
    exe = str(Path(sys.argv[1]).resolve())
    check_fixtures(exe)
    check_vehicles(exe)
    sys.exit(1 if FAILURES else 0)
