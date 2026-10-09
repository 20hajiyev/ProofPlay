"""Data-driven car roster (D-089, design/car_roster_plan.md).

Owner (2026-10-04): "40-50 cars inspired by real cars and cars from other games, not alike". Each
design is a row: an archetype (body form) with overrides. stations() turns it into the loft stations
Body expects; generate_tuner_cars.build_design() dresses it. Names are original, no brands: the
inspiration notes are for us, not shipped text.

Station tuple (Body): (y, half width, floor z, belt z, crown z, roof/deck half width, zone_to_next).
"""

# ---------------------------------------------------------------- archetype presets
# L length, W half width, H roof crown, wb wheelbase, track, r wheel radius, rw wheel width,
# zs floor, belt, hood (crown at the windscreen base), nose (crown at the tip),
# ws (y of the windscreen base from the front tip), rake (windscreen run), roof (roof run), tail
ARCH = {
    'hatch':    dict(L=3.95, W=0.86, H=1.40, wb=2.45, track=1.62, r=0.33, rw=0.19, zs=0.23, belt=0.88, hood=0.93, nose=0.72, ws=1.25, rake=0.62, roof=1.3, tail='hatch'),
    'kei':      dict(L=3.35, W=0.74, H=1.48, wb=2.15, track=1.38, r=0.29, rw=0.16, zs=0.22, belt=0.86, hood=0.9, nose=0.74, ws=0.85, rake=0.55, roof=1.35, tail='hatch'),
    'coupe':    dict(L=4.3, W=0.88, H=1.3, wb=2.5, track=1.68, r=0.34, rw=0.21, zs=0.21, belt=0.84, hood=0.86, nose=0.66, ws=1.55, rake=0.75, roof=0.95, tail='notch'),
    'fastback': dict(L=4.4, W=0.9, H=1.29, wb=2.55, track=1.7, r=0.35, rw=0.22, zs=0.2, belt=0.83, hood=0.86, nose=0.64, ws=1.55, rake=0.78, roof=0.6, tail='fastback'),
    'sedan':    dict(L=4.6, W=0.9, H=1.42, wb=2.7, track=1.72, r=0.34, rw=0.21, zs=0.22, belt=0.9, hood=0.94, nose=0.74, ws=1.6, rake=0.7, roof=1.25, tail='notch'),
    'wagon':    dict(L=4.75, W=0.9, H=1.44, wb=2.75, track=1.72, r=0.34, rw=0.2, zs=0.22, belt=0.9, hood=0.94, nose=0.74, ws=1.55, rake=0.68, roof=2.05, tail='wagon'),
    'muscle':   dict(L=4.85, W=0.94, H=1.3, wb=2.8, track=1.78, r=0.36, rw=0.24, zs=0.22, belt=0.86, hood=0.9, nose=0.78, ws=2.05, rake=0.7, roof=0.85, tail='fastback'),
    'pickup':   dict(L=4.9, W=0.98, H=1.75, wb=3.0, track=1.84, r=0.4, rw=0.26, zs=0.34, belt=1.12, hood=1.15, nose=0.98, ws=1.7, rake=0.5, roof=0.9, tail='bed'),
    'suv':      dict(L=4.6, W=0.96, H=1.85, wb=2.75, track=1.8, r=0.41, rw=0.26, zs=0.38, belt=1.18, hood=1.2, nose=1.05, ws=1.35, rake=0.45, roof=2.15, tail='wagon'),
    'van':      dict(L=4.8, W=0.95, H=2.05, wb=2.95, track=1.75, r=0.36, rw=0.22, zs=0.3, belt=1.12, hood=1.1, nose=0.95, ws=0.75, rake=0.55, roof=2.95, tail='wagon'),
    'mid':      dict(L=4.4, W=0.97, H=1.14, wb=2.55, track=1.74, r=0.35, rw=0.27, zs=0.18, belt=0.74, hood=0.74, nose=0.56, ws=1.4, rake=0.95, roof=0.75, tail='mid'),
    'rally':    dict(L=4.4, W=0.92, H=1.42, wb=2.6, track=1.76, r=0.35, rw=0.23, zs=0.24, belt=0.9, hood=0.93, nose=0.72, ws=1.45, rake=0.7, roof=1.2, tail='notch'),
}

# Interior style per archetype (car_cabin.STYLES keys)
CABIN = {'hatch': 'D01', 'kei': 'D01', 'coupe': 'D02', 'fastback': 'D02', 'sedan': 'D05', 'wagon': 'D05', 'muscle': 'D04',
         'pickup': 'D04', 'suv': 'D04', 'van': 'D04', 'mid': 'D02', 'rally': 'D03'}

# Physics class targets (top speed km/h)
CLASS_TOP = {'D': 170, 'C': 190, 'B': 215, 'A': 240}


def stations(a):
    """Loft stations for a design from its archetype numbers (front tip at +L/2)."""
    L, W, H, zs, belt, hood, nose = a['L'], a['W'], a['H'], a['zs'], a['belt'], a['hood'], a['nose']
    yf, yr = L / 2, -L / 2
    y_ws = yf - a['ws']
    y_r0 = y_ws - a['rake']
    y_r1 = y_r0 - a['roof']
    tw = W * a.get('tumble', 0.74)      # roof half width
    deck_w = W - 0.1
    st = [
        (yf, W - 0.1, zs + 0.03, nose - 0.06, nose, (W - 0.1) * 0.86, 'bonnet'),
        (yf - 0.08, W - 0.03, zs + 0.01, nose + 0.04, nose + 0.08, W - 0.12, 'bonnet'),
        (yf - 0.38, W - 0.01, zs, nose + (hood - nose) * 0.55 - 0.03, nose + (hood - nose) * 0.55 + 0.03, deck_w, 'bonnet'),
    ]
    if y_ws < yf - 0.75:
        st.append((y_ws + 0.35, W, zs, hood - 0.07, hood - 0.02, deck_w, 'bonnet'))
    st.append((y_ws, W, zs, min(belt, hood - 0.04), hood, W - 0.08, 'ws'))
    st.append((y_r0, W - 0.015, zs, belt + 0.02, H, tw, 'roof'))
    t = a['tail']
    if t == 'hatch':
        st += [(y_r1, W - 0.01, zs, belt + 0.03, H - 0.02, tw + 0.02, 'rw'),
               (yr + 0.16, W + 0.005, zs, belt + 0.05, belt + 0.27, W - 0.13, 'deck'),
               (yr + 0.05, W - 0.005, zs + 0.01, belt + 0.05, belt + 0.21, W - 0.15, 'deck'),
               (yr, W - 0.025, zs + 0.03, belt + 0.03, belt + 0.16, W - 0.16, None)]
    elif t == 'notch':
        st += [(y_r1, W - 0.01, zs, belt + 0.03, H - 0.02, tw + 0.02, 'rw'),
               (y_r1 - min(0.62, 0.6 * (y_r1 - yr - 0.1)), W, zs, belt + 0.05, belt + 0.17, deck_w, 'deck'),
               (yr + 0.1, W - 0.01, zs, belt + 0.06, belt + 0.16, deck_w - 0.02, 'deck'),
               (yr + 0.03, W - 0.04, zs + 0.02, belt + 0.04, belt + 0.13, deck_w - 0.06, 'deck'),
               (yr, W - 0.08, zs + 0.05, belt + 0.01, belt + 0.09, deck_w - 0.1, None)]
    elif t == 'fastback':
        st += [(y_r1, W - 0.01, zs, belt + 0.03, H - 0.01, tw, 'rw'),
               (yr + 0.5, W + 0.01, zs, belt + 0.05, belt + 0.14, deck_w, 'deck'),
               (yr + 0.08, W - 0.01, zs + 0.01, belt + 0.06, belt + 0.12, deck_w - 0.02, 'deck'),
               (yr + 0.02, W - 0.05, zs + 0.03, belt + 0.05, belt + 0.1, deck_w - 0.06, 'deck'),
               (yr, W - 0.1, zs + 0.06, belt + 0.02, belt + 0.07, deck_w - 0.1, None)]
    elif t == 'bed':
        y_bed = y_r1 - 0.17
        st += [(y_r1, W, zs, belt + 0.02, H - 0.02, tw, 'rw'),
               (y_bed, W + 0.01, zs, belt + 0.07, belt + 0.12, W - 0.03, 'bed'),
               (yr + 0.15, W + 0.01, zs, belt + 0.08, belt + 0.12, W - 0.03, 'deck'),
               (yr + 0.05, W - 0.03, zs + 0.01, belt + 0.06, belt + 0.1, W - 0.06, 'deck'),
               (yr, W - 0.08, zs + 0.03, belt + 0.03, belt + 0.07, W - 0.1, None)]
    elif t == 'wagon':
        st += [(y_r1, W - 0.005, zs, belt + 0.03, H - 0.03, tw + 0.03, 'rw'),
               (yr + 0.12, W, zs, belt + 0.04, belt + 0.32, W - 0.1, 'deck'),
               (yr + 0.04, W - 0.02, zs + 0.01, belt + 0.03, belt + 0.25, W - 0.12, 'deck'),
               (yr, W - 0.05, zs + 0.03, belt + 0.01, belt + 0.18, W - 0.14, None)]
    elif t == 'mid':  # cabin forward; a long glass engine cover over a low deck
        st += [(y_r1, W - 0.02, zs, belt + 0.02, H - 0.03, tw, 'rw'),
               (y_r1 - min(0.9, 0.6 * (y_r1 - yr - 0.12)), W + 0.03, zs, belt + 0.05, belt + 0.18, deck_w, 'deck'),
               (yr + 0.12, W + 0.02, zs, belt + 0.07, belt + 0.16, deck_w, 'deck'),
               (yr + 0.03, W - 0.02, zs + 0.02, belt + 0.05, belt + 0.12, deck_w - 0.05, 'deck'),
               (yr, W - 0.07, zs + 0.05, belt + 0.02, belt + 0.08, deck_w - 0.1, None)]
    # any station that a short car's numbers pushed out of order is dropped (the last one stays)
    out = [st[0]]
    for s_ in st[1:]:
        if s_[0] < out[-1][0] - 0.02:
            out.append(s_)
        elif s_[6] is None:
            out[-1] = out[-1][:6] + (None,)
    return out


def D(cid, name, arch, cls, note, **kw):
    """One design: the archetype numbers, then this car's overrides."""
    d = dict(ARCH[arch])
    d.update(id=cid, name=name, arch=arch, cls=cls, note=note)
    if isinstance(kw.get('tail'), tuple):  # tail_l=(...) is the tail lamps; a string is the body's tail form
        kw['tail_l'] = kw.pop('tail')
    d.update(kw)
    d.setdefault('tail_l', ('block', 0.6, 0.7, 0.3, 0.12))
    return d


# ---------------------------------------------------------------- the roster (42 new cars)
# head=(style, x_frac, z_frac, w_frac, h): style quad|single|round|twin_round|slim|popup|flush
# tail_l=(style, ...): block|bar|round|vertical ; grille=(style, w_frac, h) honey|slats|chrome|none
DESIGNS = [
    # ---- class D: cheap and cheerful
    D('D06', 'Kolkhoz 1300', 'sedan', 'D', 'Soviet 70s boxy saloon (Zhiguli): round twin lamps, chrome grille', L=4.1, W=0.81, H=1.44, wb=2.42, track=1.6, r=0.32, belt=0.9, hood=0.92, nose=0.78, ws=1.25, rake=0.55, roof=1.2, tumble=0.82,
      head=('twin_round', 0.66, 0.62, 0.28, 0.13), tail=('block', 0.6, 0.66, 0.28, 0.12), grille=('chrome', 0.62, 0.1), bumper='chrome', drive='RWD',
      paint=(0.72, 0.84, 0.86), accent=(0.95, 0.95, 0.92), rim='five', rim_rgb=(0.85, 0.85, 0.85), exhaust='single', spoiler='none', hood_s='stock', stripe='none', audio='I4_NA'),
    D('D07', 'Kremlin 24', 'sedan', 'D', 'big 70s Soviet staff saloon (Volga): long, chrome everywhere, round lamps', L=4.75, W=0.9, H=1.47, wb=2.8, belt=0.92, hood=0.96, nose=0.82, ws=1.6, rake=0.6, roof=1.35, tumble=0.8,
      head=('round', 0.64, 0.6, 0.24, 0.17), tail=('vertical', 0.78, 0.66, 0.12, 0.3), grille=('chrome', 0.9, 0.16), bumper='chrome', drive='RWD',
      paint=(0.12, 0.13, 0.15), accent=(0.85, 0.85, 0.8), rim='dish', rim_rgb=(0.85, 0.85, 0.85), exhaust='single', spoiler='none', hood_s='stock', stripe='none', audio='V6'),
    D('D08', 'Bento', 'kei', 'D', 'Japanese kei box (tall, tiny, square lamps)', head=('single', 0.62, 0.6, 0.3, 0.12), tail=('vertical', 0.86, 0.75, 0.1, 0.32), grille=('slats', 0.5, 0.06),
      drive='FWD', paint=(0.98, 0.72, 0.2), accent=(0.98, 0.98, 0.98), rim='ten', rim_rgb=(0.9, 0.9, 0.9), exhaust='single', spoiler='lip', hood_s='stock', stripe='twin', audio='I4_NA'),
    D('D09', 'Pip', 'hatch', 'D', 'classic British micro (Mini): short, round lamps on the wings, roof contrasting', L=3.4, W=0.76, H=1.36, wb=2.05, track=1.4, r=0.28, rw=0.16, ws=0.98, rake=0.5, roof=1.2, tumble=0.84,
      head=('round', 0.6, 0.62, 0.24, 0.17), tail=('block', 0.62, 0.66, 0.2, 0.14), grille=('chrome', 0.58, 0.12), bumper='chrome', drive='FWD',
      paint=(0.12, 0.42, 0.22), accent=(0.97, 0.97, 0.95), rim='mesh', rim_rgb=(0.85, 0.85, 0.85), exhaust='single', spoiler='none', hood_s='stock', stripe='center', roof_color=(0.97, 0.97, 0.95), audio='I4_NA'),
    D('D10', 'Rabbit GTX', 'hatch', 'D', 'late-70s German hot hatch (Golf Mk1 GTI): square lamps, red grille pinstripe', L=3.8, W=0.82, H=1.38, ws=1.1, rake=0.58, roof=1.25,
      head=('round', 0.62, 0.62, 0.26, 0.16), tail=('block', 0.6, 0.7, 0.3, 0.12), grille=('slats', 0.86, 0.12), drive='FWD',
      paint=(0.85, 0.85, 0.83), accent=(0.75, 0.05, 0.05), rim='fan', rim_rgb=(0.2, 0.2, 0.22), exhaust='single', spoiler='lip', hood_s='stock', stripe='side', audio='I4_NA'),
    D('D11', 'Doodlebug', 'fastback', 'D', 'rear-engined people\'s car (Beetle): rounded, tall roof, no grille', L=4.05, W=0.8, H=1.48, wb=2.4, track=1.4, r=0.33, belt=0.84, hood=0.84, nose=0.72, ws=1.2, rake=0.5, roof=0.8, tumble=0.78,
      head=('round', 0.66, 0.72, 0.2, 0.18), tail=('vertical', 0.7, 0.72, 0.1, 0.16), grille=None, bumper='chrome', drive='RWD',
      paint=(0.45, 0.68, 0.85), accent=(0.95, 0.95, 0.9), rim='dish', rim_rgb=(0.92, 0.92, 0.9), exhaust='dual', spoiler='none', hood_s='stock', stripe='none', audio='I4_NA'),
    D('D12', 'Fjord 245', 'wagon', 'D', 'Swedish 80s brick estate (Volvo 240): square everything', W=0.87, H=1.46, tumble=0.86,
      head=('single', 0.62, 0.62, 0.34, 0.14), tail=('vertical', 0.84, 0.72, 0.12, 0.42), grille=('slats', 0.64, 0.14), drive='RWD',
      paint=(0.55, 0.12, 0.12), accent=(0.1, 0.1, 0.12), rim='ten', rim_rgb=(0.75, 0.75, 0.75), exhaust='single', spoiler='none', hood_s='stock', stripe='none', audio='I4_NA'),
    D('D13', 'Metro Cab', 'sedan', 'D', 'big American taxi (Crown Victoria / Crazy Taxi cab): roof sign, checker band', L=5.0, W=0.95, H=1.44, wb=2.95, track=1.78, r=0.35,
      head=('single', 0.64, 0.62, 0.3, 0.12), tail=('bar', 0.0, 0.7, 0.0, 0.1), grille=('chrome', 0.7, 0.14), drive='RWD',
      paint=(0.98, 0.78, 0.08), accent=(0.08, 0.08, 0.08), rim='ten', rim_rgb=(0.7, 0.7, 0.7), exhaust='single', spoiler='none', hood_s='stock', stripe='checker', taxi_sign=True, audio='V8'),
    D('D14', 'Steppe 4x4', 'suv', 'D', 'Soviet small 4x4 (Niva / UAZ): short, tall, round lamps, spare on the tail', L=3.8, W=0.84, H=1.75, wb=2.2, track=1.48, r=0.37, rw=0.2, ws=1.1, roof=1.5, tumble=0.85,
      head=('round', 0.62, 0.6, 0.24, 0.16), tail=('vertical', 0.82, 0.62, 0.1, 0.22), grille=('slats', 0.6, 0.14), drive='AWD',
      paint=(0.32, 0.4, 0.22), accent=(0.9, 0.85, 0.7), rim='five', rim_rgb=(0.3, 0.3, 0.32), exhaust='single', spoiler='none', hood_s='stock', stripe='none', roof_s='rack', spare=True, audio='I4_NA'),
    D('D15', 'Courier', 'van', 'D', 'one-box delivery van (Transit / Hiace): tall, flat face, sliding door', head=('single', 0.64, 0.66, 0.3, 0.14), tail=('vertical', 0.86, 0.85, 0.1, 0.42), grille=('slats', 0.74, 0.16),
      drive='RWD', paint=(0.92, 0.92, 0.9), accent=(0.1, 0.35, 0.8), rim='five', rim_rgb=(0.6, 0.6, 0.62), exhaust='single', spoiler='none', hood_s='stock', stripe='side', audio='I4_TURBO'),
    # ---- class C: the sports-car heroes of the 80s-90s
    D('C01', 'Tofu 86', 'coupe', 'C', '80s Japanese hatchback coupe (AE86): pop-up lamps, two-tone panda', L=4.2, W=0.83, H=1.31, tail='hatch', ws=1.4, roof=1.05,
      head=('popup', 0.6, 0.52, 0.3, 0.06), tail_l=('bar', 0.0, 0.7, 0.0, 0.11), grille=None, drive='RWD',
      paint=(0.96, 0.96, 0.95), accent=(0.08, 0.08, 0.1), rim='mesh', rim_rgb=(0.85, 0.85, 0.85), exhaust='single', spoiler='lip', hood_s='stock', stripe='panda', audio='I4_NA'),
    D('C02', 'Siva S13', 'coupe', 'C', '90s Japanese drift coupe (Silvia S13): slim lamps, notch tail', head=('slim', 0.6, 0.6, 0.36, 0.08), tail=('bar', 0.0, 0.68, 0.0, 0.1), grille=('slats', 0.5, 0.05),
      drive='RWD', paint=(0.6, 0.12, 0.45), accent=(0.95, 0.95, 0.95), rim='dish', rim_rgb=(0.95, 0.95, 0.95), exhaust='single', spoiler='lip', hood_s='stock', stripe='none', audio='I4_TURBO'),
    D('C03', 'Kaiser M', 'coupe', 'C', 'German 80s touring car (E30 M3): boxy, flares, quad round lamps, kidney grille', L=4.35, W=0.87, H=1.36, tumble=0.82,
      head=('twin_round', 0.6, 0.62, 0.3, 0.12), tail=('block', 0.6, 0.7, 0.32, 0.12), grille=('twin_kidney', 0.3, 0.12), drive='RWD', flares='rally',
      paint=(0.95, 0.95, 0.95), accent=(0.1, 0.35, 0.8), rim='ten', rim_rgb=(0.85, 0.85, 0.85), exhaust='dual', spoiler='lip', hood_s='stock', stripe='tricolor', audio='I4_NA'),
    D('C04', 'Lancer Rex', 'rally', 'C', 'Japanese rally sedan (Lancer Evo): big intake, tall wing', head=('slim', 0.6, 0.62, 0.36, 0.09), tail=('block', 0.62, 0.7, 0.3, 0.12), grille=('honey', 0.6, 0.1),
      drive='AWD', paint=(0.85, 0.1, 0.1), accent=(0.1, 0.1, 0.1), rim='y_spoke', rim_rgb=(0.2, 0.2, 0.22), exhaust='single', spoiler='gt', aero_f='splitter', hood_s='vent', stripe='none', audio='I4_TURBO'),
    D('C05', 'Pleiad 22', 'rally', 'C', 'Japanese rally coupe (Impreza 22B): blue, gold rims, wide flares, bonnet scoop', L=4.35, tail='notch', head=('quad', 0.62, 0.62, 0.32, 0.11), tail_l=('round', 0.62, 0.7, 0.22, 0.12), grille=('honey', 0.42, 0.1),
      drive='AWD', paint=(0.12, 0.25, 0.7), accent=(0.95, 0.75, 0.1), rim='mesh', rim_rgb=(0.95, 0.75, 0.2), exhaust='single', spoiler='wing', hood_s='vent', flares='widebody', stripe='none', roof_s='scoop', audio='I4_TURBO'),
    D('C06', 'Bronco 67', 'muscle', 'C', '60s American fastback pony car (Mustang): long bonnet, slat grille', head=('round', 0.7, 0.64, 0.2, 0.16), tail=('vertical', 0.66, 0.68, 0.24, 0.12), grille=('slats', 1.0, 0.18),
      drive='RWD', paint=(0.1, 0.3, 0.18), accent=(0.95, 0.95, 0.92), rim='five', rim_rgb=(0.85, 0.85, 0.85), exhaust='dual', spoiler='lip', hood_s='vent', stripe='center', bumper='chrome', audio='V8'),
    D('C07', 'Thunder 69', 'muscle', 'C', '60s American muscle saloon (Charger): hidden lamps behind a full-width grille', tail='notch', roof=1.1, head=('hidden', 0.0, 0.62, 0.0, 0.14), tail_l=('bar', 0.0, 0.7, 0.0, 0.12), grille=('full', 1.6, 0.18),
      drive='RWD', paint=(0.85, 0.42, 0.05), accent=(0.08, 0.08, 0.08), rim='dish', rim_rgb=(0.8, 0.8, 0.82), exhaust='dual', spoiler='none', hood_s='vent', stripe='tail_band', audio='V8'),
    D('C08', 'Rattler SS', 'muscle', 'C', '60s American muscle coupe (Camaro): round lamps, twin stripes', tail='notch', roof=0.95, head=('round', 0.72, 0.64, 0.2, 0.16), tail_l=('block', 0.6, 0.68, 0.3, 0.1), grille=('honey', 1.2, 0.16),
      drive='RWD', paint=(0.98, 0.78, 0.1), accent=(0.05, 0.05, 0.05), rim='five', rim_rgb=(0.85, 0.85, 0.85), exhaust='dual', spoiler='lip', hood_s='stock', stripe='twin', audio='V8'),
    D('C09', 'Dune Hauler', 'pickup', 'C', 'Japanese off-road pickup (Hilux): lifted, bull bar, light bar', head=('single', 0.66, 0.64, 0.28, 0.14), tail=('vertical', 0.88, 0.78, 0.1, 0.3), grille=('slats', 1.0, 0.24),
      drive='AWD', paint=(0.85, 0.2, 0.1), accent=(0.95, 0.95, 0.95), rim='ten', rim_rgb=(0.25, 0.25, 0.27), exhaust='single', spoiler='none', hood_s='stock', stripe='side', bull_bar=True, lights_s='bar', audio='V6'),
    D('C10', 'Brick G', 'suv', 'C', 'square 80s luxury off-roader (G-Class / Land Cruiser): flat glass, round lamps, spare', head=('round', 0.68, 0.62, 0.22, 0.16), tail=('vertical', 0.86, 0.72, 0.1, 0.3), grille=('slats', 0.66, 0.2),
      drive='AWD', paint=(0.1, 0.1, 0.11), accent=(0.75, 0.75, 0.78), rim='five', rim_rgb=(0.75, 0.75, 0.78), exhaust='single', spoiler='none', hood_s='stock', stripe='none', spare=True, tumble=0.9, audio='V8'),
    D('C11', 'Flux 81', 'coupe', 'C', '80s stainless wedge with gullwings (DeLorean / Back to the Future)', L=4.25, H=1.15, belt=0.82, hood=0.82, nose=0.6, ws=1.5, rake=0.82, roof=0.8, tail='mid',
      head=('slim', 0.6, 0.58, 0.36, 0.08), tail_l=('bar', 0.0, 0.66, 0.0, 0.1), grille=('slats', 0.8, 0.06), drive='RWD',
      paint=(0.62, 0.63, 0.66), accent=(0.1, 0.1, 0.12), rim='fan', rim_rgb=(0.7, 0.7, 0.72), exhaust='dual', spoiler='none', hood_s='stock', stripe='none', metal=0.5, audio='V6'),
    D('C12', 'Lion T16', 'hatch', 'C', 'Group B mid-engined rally hatch (205 T16): flared, white with stripes', W=0.9, belt=0.88, flares='rally',
      head=('slim', 0.62, 0.62, 0.32, 0.09), tail=('block', 0.62, 0.7, 0.26, 0.12), grille=('honey', 0.6, 0.08), drive='AWD',
      paint=(0.96, 0.96, 0.96), accent=(0.1, 0.2, 0.7), rim='mesh', rim_rgb=(0.95, 0.95, 0.95), exhaust='single', spoiler='wing', aero_f='splitter', hood_s='vent', stripe='rally', roof_s='scoop', audio='I4_TURBO'),
    # ---- class B: 90s-modern performance
    D('B01', 'Sopra 4', 'fastback', 'B', '90s Japanese GT (Supra Mk4): smooth fastback, hoop wing, round quad tails', H=1.27, tumble=0.7,
      head=('slim', 0.6, 0.6, 0.38, 0.1), tail=('round', 0.62, 0.7, 0.28, 0.12), grille=None, drive='RWD',
      paint=(0.92, 0.38, 0.06), accent=(0.1, 0.1, 0.1), rim='five', rim_rgb=(0.82, 0.82, 0.84), exhaust='single', spoiler='wing', aero_f='lip', hood_s='stock', stripe='none', audio='I4_TURBO'),
    D('B02', 'Rotor 7', 'fastback', 'B', '90s Japanese rotary sports car (RX-7 FD): low, curvy, pop-ups, round tails', L=4.3, H=1.23, nose=0.6, hood=0.82, belt=0.8,
      head=('popup', 0.6, 0.5, 0.3, 0.06), tail=('round', 0.6, 0.68, 0.24, 0.1), grille=None, drive='RWD',
      paint=(0.92, 0.82, 0.12), accent=(0.1, 0.1, 0.1), rim='five', rim_rgb=(0.85, 0.85, 0.87), exhaust='dual', spoiler='wing', aero_f='lip', hood_s='stock', stripe='none', audio='I4_TURBO'),
    D('B03', 'Skyrider R', 'coupe', 'B', 'Japanese AWD coupe (Skyline R34): boxy, round twin tails, carbon wing', L=4.6, W=0.9, H=1.36, tumble=0.8,
      head=('slim', 0.62, 0.62, 0.36, 0.1), tail=('round', 0.6, 0.72, 0.3, 0.12), grille=('honey', 0.8, 0.1), drive='AWD',
      paint=(0.15, 0.25, 0.65), accent=(0.1, 0.1, 0.1), rim='ten', rim_rgb=(0.3, 0.3, 0.32), exhaust='single', spoiler='wing', aero_f='splitter', hood_s='carbon', stripe='none', audio='V6'),
    D('B04', 'Nexus', 'mid', 'B', 'Japanese mid-engined supercar (NSX): low, black roof, slim lamps', H=1.17, head=('popup', 0.62, 0.5, 0.32, 0.06), tail=('bar', 0.0, 0.66, 0.0, 0.1), grille=None,
      drive='RWD', paint=(0.85, 0.1, 0.1), accent=(0.1, 0.1, 0.1), rim='five', rim_rgb=(0.85, 0.85, 0.87), exhaust='dual', spoiler='lip', hood_s='stock', stripe='none', roof_color=(0.08, 0.08, 0.09), audio='V6'),
    D('B05', 'Stuttgart 9', 'fastback', 'B', 'German rear-engined icon (911): round lamps on the wings, sloping tail, whale tail', L=4.25, W=0.88, H=1.3, nose=0.62, hood=0.82, ws=1.25, roof=0.75, tumble=0.72,
      head=('round', 0.64, 0.7, 0.2, 0.17), tail=('bar', 0.0, 0.72, 0.0, 0.08), grille=None, drive='RWD',
      paint=(0.75, 0.78, 0.8), accent=(0.1, 0.1, 0.1), rim='fan', rim_rgb=(0.8, 0.8, 0.82), exhaust='dual', spoiler='lip', hood_s='stock', stripe='none', metal=0.4, audio='I4_TURBO'),
    D('B06', 'Ringmeister', 'rally', 'B', 'Group B AWD rally coupe (Quattro): boxed flares, quad lamps', L=4.4, tail='notch', flares='widebody',
      head=('quad', 0.6, 0.62, 0.34, 0.12), tail_l=('bar', 0.0, 0.72, 0.0, 0.12), grille=('slats', 0.6, 0.12), drive='AWD',
      paint=(0.95, 0.95, 0.95), accent=(0.75, 0.08, 0.08), rim='five', rim_rgb=(0.92, 0.92, 0.92), exhaust='single', spoiler='lip', aero_f='splitter', hood_s='vent', stripe='rally', audio='I4_TURBO'),
    D('B07', 'Shard', 'pickup', 'B', 'angular steel electric pickup (Cybertruck): one wedge, light bar front and back', H=1.68, hood=1.05, nose=0.95, rake=1.1, roof=0.3, tumble=0.5,
      head=('slim', 0.0, 0.72, 0.0, 0.04), tail=('bar', 0.0, 0.85, 0.0, 0.05), grille=None, drive='AWD',
      paint=(0.66, 0.68, 0.7), accent=(0.1, 0.1, 0.12), rim='aero', rim_rgb=(0.2, 0.2, 0.22), exhaust='none', spoiler='none', hood_s='stock', stripe='none', metal=0.6, lights_s='bar', audio='EV'),
    D('B08', 'Volt S', 'fastback', 'B', 'electric luxury saloon (Model S): smooth, no grille, aero rims', L=4.95, W=0.95, H=1.38, tumble=0.72,
      head=('slim', 0.62, 0.6, 0.34, 0.08), tail=('slim_tail', 0.6, 0.72, 0.34, 0.06), grille=None, drive='AWD',
      paint=(0.95, 0.95, 0.96), accent=(0.1, 0.1, 0.12), rim='aero', rim_rgb=(0.25, 0.25, 0.28), exhaust='none', spoiler='lip', hood_s='stock', stripe='none', lights_s='drl', audio='EV'),
    D('B09', 'Executive 5', 'sedan', 'B', 'German 90s super-saloon (M5 E39): understated, quad exhausts, kidney grille', L=4.8,
      head=('twin_round', 0.6, 0.62, 0.34, 0.11), tail=('block', 0.62, 0.72, 0.34, 0.12), grille=('twin_kidney', 0.3, 0.12), drive='RWD',
      paint=(0.12, 0.2, 0.3), accent=(0.75, 0.75, 0.78), rim='y_spoke', rim_rgb=(0.8, 0.8, 0.82), exhaust='quad', spoiler='lip', hood_s='stock', stripe='none', audio='V8'),
    D('B10', 'Bulwark', 'suv', 'B', 'wide military-to-civilian truck (Hummer H1): flat, very wide, slit glass', L=4.7, W=1.08, H=1.85, belt=1.3, hood=1.25, nose=1.15, ws=1.5, roof=2.0, tumble=0.9,
      head=('single', 0.7, 0.62, 0.2, 0.12), tail=('vertical', 0.88, 0.72, 0.08, 0.2), grille=('slats', 0.7, 0.22), drive='AWD',
      paint=(0.75, 0.66, 0.45), accent=(0.1, 0.1, 0.1), rim='five', rim_rgb=(0.25, 0.25, 0.27), exhaust='single', spoiler='none', hood_s='stock', stripe='none', bull_bar=True, roof_s='rack', audio='V8'),
    D('B11', 'Sweet Tooth', 'van', 'B', 'menacing ice-cream van (Twisted Metal): clown paint, cone on the roof', head=('round', 0.66, 0.64, 0.22, 0.16), tail=('vertical', 0.86, 0.85, 0.1, 0.42), grille=('chrome', 0.74, 0.18),
      drive='RWD', paint=(0.98, 0.9, 0.95), accent=(0.95, 0.25, 0.55), rim='ten', rim_rgb=(0.9, 0.9, 0.9), exhaust='side', spoiler='none', hood_s='stock', stripe='checker', ice_cream=True, audio='V8'),
    D('B12', 'Banshi', 'muscle', 'B', 'long-bonnet V10 roadster (Viper / GTA Banshee): side exhausts, twin stripes', L=4.5, H=1.18, nose=0.62, hood=0.82, ws=2.0, roof=0.6, tumble=0.7,
      head=('slim', 0.66, 0.62, 0.3, 0.09), tail=('bar', 0.0, 0.66, 0.0, 0.08), grille=('honey', 0.5, 0.08), drive='RWD',
      paint=(0.1, 0.2, 0.75), accent=(0.97, 0.97, 0.97), rim='five', rim_rgb=(0.85, 0.85, 0.87), exhaust='side', spoiler='lip', hood_s='vent', stripe='twin', audio='V8'),
    # ---- class A: supercars
    D('A01', 'Contrast', 'mid', 'A', '70s wedge supercar (Countach): flat panels, giant wing, scissor stance', L=4.15, W=1.0, H=1.07, nose=0.5, hood=0.7, belt=0.72, rake=1.05,
      head=('popup', 0.62, 0.48, 0.3, 0.05), tail=('block', 0.6, 0.66, 0.34, 0.1), grille=None, drive='RWD',
      paint=(0.95, 0.85, 0.1), accent=(0.1, 0.1, 0.1), rim='fan', rim_rgb=(0.85, 0.85, 0.87), exhaust='quad', spoiler='gt', hood_s='vent', stripe='none', audio='V8'),
    D('A02', 'Quaranta', 'mid', 'A', 'late-80s turbo supercar (F40): red, slatted rear glass, full-width wing', L=4.4, H=1.12,
      head=('popup', 0.62, 0.5, 0.32, 0.06), tail=('round', 0.62, 0.66, 0.26, 0.1), grille=('slats', 0.6, 0.06), drive='RWD',
      paint=(0.85, 0.06, 0.06), accent=(0.1, 0.1, 0.1), rim='five', rim_rgb=(0.85, 0.85, 0.87), exhaust='single', spoiler='gt', hood_s='vent', stripe='none', audio='V8'),
    D('A03', 'Redhead', 'mid', 'A', '80s flat-12 GT (Testarossa): very wide hips, side strakes', L=4.48, W=1.0, H=1.13,
      head=('popup', 0.62, 0.5, 0.3, 0.05), tail=('slats_tail', 0.0, 0.68, 0.0, 0.12), grille=('slats', 1.2, 0.08), drive='RWD',
      paint=(0.92, 0.92, 0.9), accent=(0.1, 0.1, 0.1), rim='five', rim_rgb=(0.85, 0.85, 0.87), exhaust='dual', spoiler='none', hood_s='stock', stripe='strakes', audio='V8'),
    D('A04', 'Toro 66', 'mid', 'A', '60s first mid-engine supercar (Miura): curvy, eyelash lamps, louvred tail', L=4.36, H=1.06, nose=0.54,
      head=('round', 0.64, 0.66, 0.22, 0.12), tail=('block', 0.6, 0.66, 0.3, 0.08), grille=('slats', 0.7, 0.08), drive='RWD',
      paint=(0.95, 0.55, 0.05), accent=(0.95, 0.85, 0.5), rim='mesh', rim_rgb=(0.85, 0.75, 0.4), exhaust='quad', spoiler='none', hood_s='vent', stripe='none', audio='V8'),
    D('A05', 'Mans 40', 'mid', 'A', 'Le Mans endurance racer (GT40): very low, racing stripes and roundels', L=4.2, H=1.02, nose=0.5, hood=0.68, belt=0.7,
      head=('round', 0.64, 0.6, 0.2, 0.12), tail=('round', 0.62, 0.62, 0.24, 0.1), grille=('honey', 0.6, 0.06), drive='RWD',
      paint=(0.55, 0.75, 0.92), accent=(0.98, 0.55, 0.05), rim='mesh', rim_rgb=(0.85, 0.85, 0.87), exhaust='quad', spoiler='lip', hood_s='vent', stripe='center', audio='V8'),
    D('A06', 'Molsheim 16', 'mid', 'A', 'modern 16-cylinder hypercar (Veyron / Chiron): horseshoe grille, two-tone, rounded', L=4.5, W=1.0, H=1.2,
      head=('quad', 0.62, 0.58, 0.32, 0.08), tail=('bar', 0.0, 0.66, 0.0, 0.08), grille=('horseshoe', 0.3, 0.2), drive='AWD',
      paint=(0.05, 0.12, 0.35), accent=(0.75, 0.78, 0.82), rim='split', rim_rgb=(0.85, 0.85, 0.87), exhaust='quad', spoiler='lip', hood_s='stock', stripe='two_tone', audio='V8'),
    D('A07', 'Zenda', 'mid', 'A', 'Italian boutique hypercar (Zonda): quad central exhaust, fighter-jet canopy', L=4.45, H=1.14, rake=1.1, roof=0.65,
      head=('quad', 0.62, 0.56, 0.32, 0.08), tail=('round', 0.62, 0.66, 0.26, 0.1), grille=('honey', 0.8, 0.08), drive='RWD',
      paint=(0.2, 0.22, 0.24), accent=(0.95, 0.75, 0.1), rim='y_spoke', rim_rgb=(0.85, 0.85, 0.87), exhaust='center', spoiler='gt', hood_s='carbon', stripe='none', audio='V8'),
    D('A08', 'Inferno', 'mid', 'A', 'fictional open-world supercar (GTA Infernus / NFS heroes): wedge, neon accents', L=4.5, W=1.0, H=1.1, nose=0.5,
      head=('slim', 0.62, 0.52, 0.34, 0.05), tail=('bar', 0.0, 0.64, 0.0, 0.06), grille=('honey', 1.0, 0.06), drive='AWD',
      paint=(0.92, 0.92, 0.94), accent=(0.1, 0.85, 0.85), rim='aero', rim_rgb=(0.15, 0.15, 0.17), exhaust='dual', spoiler='wing', aero_f='splitter', hood_s='carbon', stripe='side', lights_s='drl', audio='V8'),
]


def check_unique():
    """No two designs share archetype + head lamps + tail lamps + grille + paint (owner: not alike)."""
    seen = {}
    for d in DESIGNS:
        key = (d['arch'], d['head'][0], d['tail_l'][0], (d['grille'] or ('none',))[0], d['paint'])
        if key in seen:
            raise ValueError(f"{d['id']} looks like {seen[key]}: {key}")
        seen[key] = d['id']
    return len(DESIGNS)


if __name__ == '__main__':
    import collections
    print(check_unique(), 'designs, unique')
    print(collections.Counter(d['cls'] for d in DESIGNS), collections.Counter(d['arch'] for d in DESIGNS))
    for d in DESIGNS:
        st = stations(d)
        ys = [s_[0] for s_ in st]
        assert all(a_ > b_ for a_, b_ in zip(ys, ys[1:])), (d['id'], ys)
        assert all(s_[3] < s_[4] for s_ in st), (d['id'], 'belt above crown')
