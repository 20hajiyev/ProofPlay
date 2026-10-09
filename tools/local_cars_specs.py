"""Real-ish physics for the owner's local real-brand cars (D-048).
Run: python tools/local_cars_specs.py        (after tools/local_cars_import.py)

The importer copies physics from a class D base car, so every local car drove like a D car.
This applies approximate public figures per model from assets/local_only/cars/cars.json "specs"
to assets/local_only/cars/out/<id>.json. Derived values:
  final_drive   so top gear reaches top_speed_kmh at ~97% of max_rpm (the car is gear-limited)
  brake torque  scales with mass (race cars stronger)
  health        scales with mass (heavier = sturdier)
Specs are approximations for private play, not certified data.
"""
import json, math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / 'assets' / 'local_only' / 'cars'
# drivetrain, mass kg, peak torque Nm, top km/h, max rpm, audio family, grip (tyre multiplier), race car
SPECS = {
    'L01': ('RWD', 1750, 650, 240, 5500, 'V8', 0.95, False),  # Dodge Charger R/T 1970 (440 V8)
    'L02': ('RWD', 1230, 235, 230, 7000, 'I4_NA', 1.0, False),  # Mercedes 190E 2.3-16
    'L03': ('RWD', 1620, 650, 317, 7200, 'V8', 1.1, False),  # Mercedes SLS AMG
    'L04': ('RWD', 1100, 500, 280, 9000, 'V8', 1.35, True),  # C-Coupe DTM 2018
    'L05': ('RWD', 1285, 650, 290, 7000, 'V6', 1.35, True),  # GT-R Nismo GT3
    'L06': ('RWD', 1300, 470, 290, 9400, 'V6', 1.35, True),  # 992 GT3 R (flat-six)
    'L07': ('RWD', 1035, 121, 150, 5600, 'I4_NA', 0.85, False),  # Lada 2106
    'L08': ('RWD', 1030, 105, 150, 5600, 'I4_NA', 0.85, False),  # Lada 2103
    'L09': ('AWD', 1560, 392, 250, 8000, 'I4_TURBO', 1.05, False),  # Skyline R34 (tuned)
    'L10': ('AWD', 1740, 632, 315, 7000, 'V6', 1.1, False),  # GT-R R35
    'L11': ('RWD', 1150, 400, 280, 8500, 'V6', 1.3, True),  # 996 GT300
    'L12': ('RWD', 1570, 431, 270, 6800, 'I4_TURBO', 1.0, False),  # Supra Mk IV (tuned)
    'L13': ('RWD', 1330, 1000, 390, 7500, 'V8', 1.15, False),  # Koenigsegg Agera
    'L14': ('RWD', 1430, 440, 305, 7500, 'V6', 1.1, False),  # 911 Carrera S 2015
    'L15': ('RWD', 1530, 637, 312, 6500, 'V8', 1.1, False),  # Corvette C8
    'L16': ('AWD', 1595, 720, 355, 8500, 'V8', 1.15, False),  # Countach LPI 800-4 (V12)
    'L17': ('RWD', 1200, 240, 235, 7250, 'I4_NA', 1.0, False),  # BMW M3 E30
    'L18': ('RWD', 1300, 380, 190, 5000, 'V8', 0.9, False),  # Mustang 1965
    'L19': ('RWD', 1900, 847, 290, 7500, 'V8', 1.05, False),  # Shelby GT500
    'L20': ('RWD', 1195, 430, 260, 5500, 'V6', 0.95, False),  # 911 Turbo 930 (flat-six)
    'L21': ('FWD', 945, 106, 155, 5600, 'I4_NA', 0.9, False),  # Lada 2109
    'L22': ('AWD', 1210, 129, 142, 5400, 'I4_NA', 0.9, False),  # Lada Niva
    'L23': ('AWD', 1550, 720, 355, 8500, 'V8', 1.15, False),  # Aventador Ultimae (V12)
    'L25': ('AWD', 1360, 373, 240, 7000, 'I4_TURBO', 1.05, False),  # Lancer Evo VI
    'L26': ('AWD', 1470, 407, 250, 7000, 'I4_TURBO', 1.05, False),  # Impreza WRX STI 2004
}

out = BASE / 'out'
report = []
for key, (drive, mass, torque, top, rpm, family, grip, race) in SPECS.items():
    path = out / f'{key}.json'
    if not path.exists():
        continue
    d = json.loads(path.read_text(encoding='utf-8'))
    top_gear = d['gear_ratios'][-1]
    wheel_r = d['wheel_radius_m']
    v = top / 3.6
    final = rpm * 0.97 * 2 * math.pi * wheel_r / 60 / (top_gear * v)
    final = min(max(final, 2.0), 6.0)
    reached = rpm * 0.97 * 2 * math.pi * wheel_r / 60 / (top_gear * final) * 3.6
    d.update({
        'drivetrain': drive, 'mass_kg': mass, 'max_engine_torque_nm': torque, 'top_speed_target_kmh': top,
        'max_rpm': rpm, 'shift_up_rpm': int(rpm * 0.93), 'shift_down_rpm': int(rpm * 0.45),
        'final_drive': round(final, 3), 'audio_family': family,
        'brake_torque_nm': int(mass * (2.2 if race else 1.6)),
        'handbrake_torque_nm': int(mass * 1.3),
        'health': int(min(150, max(80, 60 + mass * 0.035))),
        'tires': {'longitudinal_grip': grip, 'lateral_grip': grip},
    })
    # Torque curve keeps its shape; its rpm points stretch to the new rev range.
    old_max = max(p[0] for p in d['torque_curve'])
    d['torque_curve'] = [[int(p[0] * rpm / old_max), p[1]] for p in d['torque_curve']]
    d['min_rpm'] = min(d['min_rpm'], d['torque_curve'][0][0])
    path.write_text(json.dumps(d, indent=2), encoding='utf-8')
    report.append(f'{key} {drive} {mass} kg {torque} Nm {top} km/h final {final:.2f} (gear-limited {reached:.0f} km/h)')
print('\n'.join(report))
print('SPECS_APPLIED', len(report))
