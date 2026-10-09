"""Cabin v3 and the driver character (D-063). Imported by tools/generate_tuner_cars.py.

Owner (2026-09-30): "the interior is still bad - look at every detail, test a lot, make it very
detailed; the hand movement, wheel and everything else must be animated in detail; the driver is
bad - make characters like The Walking Dead (Telltale) or Borderlands".

Style: comic cel-shaded - big readable shapes, a strong jaw, heavy brows, dark ink hatching lines
on the face (the Borderlands look), all drawn by the game's cartoon shader and ink pass.

Every moving part is a named pivot the game animates (racer_sandbox RaceCarFeatures.cpp):
  STEER_PIVOT, HAND_L/R, GRIP_L/R, UPPER_ARM_L/R, FOREARM_L/R (two-bone IK), DRIVER_TORSO,
  DRIVER_HEAD, DRIVER_EYE, GEAR_LEVER (+ GEAR_KNOB), SHIFT_HAND, TAUNT_HAND, PEDAL_GAS/BRAKE/CLUTCH,
  HANDBRAKE_LEVER, STALK_L (indicators), STALK_R (wipers), FRESHENER, NEEDLE_SPEED/REV/FUEL/TEMP.
Axes (Blender): +Y car front, -X driver side (left-hand drive), +Z up.
"""
import math
import random
import bmesh
from mathutils import Vector

# The driver is a separate character (tools/generate_driver.py, D-076) of one fixed size; each cabin
# places its seat so the hip-to-eye height is HEAD_H, like a real car fits its seat to a person.
HEAD_H = 0.72
LEG_UPPER, LEG_LOWER = 0.46, 0.46

H = None  # helper namespace from generate_tuner_cars (box, cyl, torus, tube_along, link, ...)


def setup(helpers):
    global H
    H = helpers


def _sphere(name, loc, r, m, scale=(1, 1, 1), seg=20, rings=10):
    H['bpy'].ops.mesh.primitive_uv_sphere_add(segments=seg, ring_count=rings, radius=r, location=loc)
    o = H['bpy'].context.object
    o.name = name
    o.scale = scale
    o.data.materials.append(m)
    return o


def _cone(name, loc, r1, r2, depth, m, rot=(0, 0, 0), verts=12):
    H['bpy'].ops.mesh.primitive_cone_add(vertices=verts, radius1=r1, radius2=r2, depth=depth, location=loc, rotation=rot)
    o = H['bpy'].context.object
    o.name = name
    o.data.materials.append(m)
    return o


# Interior styles (D-070, owner: "make every car's interior a bit different, like real life").
# Each car's cabin takes the look of a real-world type it is inspired by.
STYLES = {
    # 80s hot hatch: plaid cloth seats, 4-spoke wheel, golf-ball knob, cassette deck, red piping
    'D01': dict(dash=(0.12, 0.12, 0.13), seat=(0.26, 0.27, 0.3), bolster=(0.15, 0.15, 0.17), insert=(0.62, 0.1, 0.09),
                headliner=(0.62, 0.62, 0.6), carpet=(0.12, 0.12, 0.13), stitch=(0.85, 0.15, 0.12), knob=(0.95, 0.95, 0.93),
                dial=(0.95, 0.95, 0.92), tick=(0.05, 0.05, 0.06), wheel='four_spoke', cluster='twin',
                drop=('roll_hoop', 'harness_bar', 'hoop_stay', 'hoop_foot', 'stopwatch', 'stopwatch_ring', 'stopwatch_crown', 'shift_led', 'led_strip')),
    # 80s mid-engine / pop-up sports coupe: wrap-around cockpit, switch pods, 5 black-faced dials
    'D02': dict(dash=(0.14, 0.15, 0.2), seat=(0.2, 0.21, 0.27), bolster=(0.12, 0.12, 0.16), insert=(0.5, 0.52, 0.58),
                headliner=(0.48, 0.49, 0.52), carpet=(0.1, 0.1, 0.13), stitch=(0.95, 0.45, 0.1), knob=(0.1, 0.1, 0.11),
                dial=(0.07, 0.07, 0.08), tick=(0.95, 0.95, 0.9), wheel='three_spoke', cluster='wrap',
                drop=('roll_hoop', 'harness_bar', 'hoop_stay', 'hoop_foot', 'stopwatch', 'stopwatch_ring', 'stopwatch_crown', 'sports_bag', 'bag_stripe', 'bag_strap')),
    # group-A rally sedan: full cage, blue shell seats with harnesses, bare metal, suede dish wheel
    'D03': dict(dash=(0.1, 0.1, 0.11), seat=(0.1, 0.2, 0.55), bolster=(0.08, 0.15, 0.42), insert=(0.08, 0.08, 0.1),
                headliner=(0.42, 0.43, 0.46), carpet=(0.3, 0.31, 0.33), stitch=(1.0, 0.8, 0.1), knob=(0.7, 0.7, 0.72),
                dial=(0.95, 0.95, 0.92), tick=(0.05, 0.05, 0.06), wheel='dish', cluster='rally',
                drop=('sunroof_frame', 'sunroof_shade', 'shade_rib', 'sunroof_handle', 'overhead_console', 'map_light', 'roof_switch', 'glasses_flap',
                      'sports_bag', 'bag_stripe', 'bag_strap', 'gauge_pod', 'pod_dial', 'pod_ring', 'pod_needle', 'rear_bench', 'rear_back',
                      'rear_headrest', 'rear_insert', 'rear_buckle', 'shelf_speaker', 'shelf_speaker_ring', 'coffee_cup', 'cup_lid', 'cup_sleeve',
                      'floor_mat', 'mat_rib', 'airbag_seam', 'phone', 'phone_clip', 'phone_screen', 'phone_route')),
    # 70s pickup: tan leather split bench, wood trim, thin 4-spoke wheel with a chrome horn ring,
    # chrome instrument panel, CB radio, fuzzy dice
    'D04': dict(dash=(0.26, 0.18, 0.12), seat=(0.66, 0.47, 0.28), bolster=(0.56, 0.38, 0.22), insert=(0.6, 0.43, 0.26),
                headliner=(0.72, 0.67, 0.56), carpet=(0.22, 0.15, 0.1), stitch=(0.95, 0.85, 0.6), knob=(0.42, 0.22, 0.1),
                dial=(0.92, 0.88, 0.75), tick=(0.08, 0.06, 0.05), wheel='classic', cluster='chrome', dice=True,
                drop=('roll_hoop', 'harness_bar', 'hoop_stay', 'hoop_foot', 'stopwatch', 'stopwatch_ring', 'stopwatch_crown', 'shift_led', 'led_strip',
                      'sunroof_frame', 'sunroof_shade', 'shade_rib', 'sunroof_handle', 'gauge_pod', 'pod_dial', 'pod_ring', 'pod_needle',
                      'sports_bag', 'bag_stripe', 'bag_strap', 'airbag_seam', 'cushion_bolster', 'back_bolster')),
    # current hot hatch (D-081): black suede and red stitching, a digital cluster behind the wheel, a
    # floating touch screen on the dash, shift lights - no cassette-era clutter
    'D05': dict(dash=(0.07, 0.07, 0.08), seat=(0.09, 0.09, 0.1), bolster=(0.06, 0.06, 0.07), insert=(0.72, 0.08, 0.08),
                headliner=(0.12, 0.12, 0.13), carpet=(0.08, 0.08, 0.09), stitch=(0.9, 0.1, 0.1), knob=(0.12, 0.12, 0.13),
                dial=(0.03, 0.04, 0.06), tick=(0.3, 0.85, 1.0), wheel='three_spoke', cluster='digital', tablet=True,
                drop=('roll_hoop', 'harness_bar', 'hoop_stay', 'hoop_foot', 'stopwatch', 'stopwatch_ring', 'stopwatch_crown',
                      'sunroof_frame', 'sunroof_shade', 'shade_rib', 'sunroof_handle', 'gauge_pod', 'pod_dial', 'pod_ring', 'pod_needle',
                      'sports_bag', 'bag_stripe', 'bag_strap', 'plaid_a', 'plaid_b', 'map')),
}


def _recolor(m, rgb):
    m.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = (*rgb, 1)


def materials(M, suit, cid='D01'):
    mat = H['mat']
    M.update({
        'headliner': mat('toon_headliner', (0.58, 0.57, 0.55), 0.9),
        'carpet': mat('toon_carpet', (0.1, 0.1, 0.11), 1.0),
        'mat_floor': mat('toon_floor_mat', (0.05, 0.05, 0.06), 1.0),
        'plastic': mat('toon_plastic', (0.16, 0.16, 0.18), 0.6),
        'dash_soft': mat('toon_dash_soft', (0.2, 0.2, 0.22), 0.8),
        'stitch': mat('toon_stitch_NoInk', (0.95, 0.45, 0.1), 0.8),
        'seat_fabric': mat('toon_seat_fabric', (0.12, 0.12, 0.14), 0.95),
        'warn_red': mat('toon_warn_red_NoInk', (1.0, 0.15, 0.1), 0.3, 0, 1.0),
        'warn_green': mat('toon_warn_green_NoInk', (0.2, 1.0, 0.3), 0.3, 0, 1.0),
        'warn_amber': mat('toon_warn_amber_NoInk', (1.0, 0.7, 0.1), 0.3, 0, 1.0),
        'warn_blue': mat('toon_warn_blue_NoInk', (0.2, 0.5, 1.0), 0.3, 0, 1.0),
        'lamp_off': mat('toon_lamp_off_NoInk', (0.1, 0.1, 0.11), 0.4),
        'dome_on': mat('toon_dome_on_NoInk', (1.0, 0.92, 0.7), 0.3, 0, 2.0),
        'freshener': mat('toon_freshener', (0.2, 0.85, 0.35), 0.8),
        'mirror_c': mat('mirror_view_C_NoInk', (0.75, 0.8, 0.85), 0.1),
        'skin': mat('toon_skin', (0.86, 0.64, 0.5), 0.8),
        'skin_shadow': mat('toon_skin_stubble', (0.62, 0.47, 0.41), 0.9),
        'hair': mat('toon_hair', (0.16, 0.1, 0.07), 0.9),
        'eye_white': mat('toon_eye_white_NoInk', (0.96, 0.95, 0.92), 0.5),
        'iris': mat('toon_iris_NoInk', (0.18, 0.35, 0.3), 0.3),
        'ink': mat('toon_ink_NoInk', (0.04, 0.03, 0.04), 1.0),
        'lips': mat('toon_lips_NoInk', (0.55, 0.3, 0.28), 0.7),
        'jacket': mat('toon_jacket', suit, 0.75),
        'jacket_dark': mat('toon_jacket_dark', tuple(c * 0.55 for c in suit), 0.8),
        'shirt': mat('toon_shirt', (0.22, 0.22, 0.25), 0.9),
        'jeans': mat('toon_jeans', (0.16, 0.22, 0.36), 0.9),
        'shoe': mat('toon_shoe', (0.85, 0.85, 0.82), 0.7),
        'shoe_dark': mat('toon_shoe_dark', (0.1, 0.1, 0.12), 0.8),
        'lens_dark': mat('toon_shades', (0.05, 0.06, 0.08), 0.1, 0.3),
        'glove': mat('toon_glove', (0.08, 0.075, 0.08), 0.7),
        'glove_pad': mat('toon_glove_pad', (0.28, 0.26, 0.26), 0.8),
        'beard': mat('toon_beard', (0.2, 0.13, 0.09), 0.95),
        'ear_in': mat('toon_ear_in', (0.72, 0.5, 0.42), 0.85),
        'glass_int': mat('toon_bottle', (0.35, 0.65, 0.8), 0.2),
        'sunroof': mat('toon_sunroof_NoInk', (0.38, 0.48, 0.58), 0.1),
        'cage': mat('toon_cage', (0.12, 0.12, 0.13), 0.4, 0.6),
        'cup': mat('toon_cup', (0.92, 0.9, 0.85), 0.8),
        'bag': mat('toon_bag', (0.18, 0.2, 0.24), 0.9),
        'map': mat('toon_map', (0.85, 0.8, 0.6), 0.9),
    })
    S = STYLES.get(cid, STYLES['D01'])
    # the cabin palette: materials the exterior pass already used (lining, seats) are recoloured in place
    for key, col in (('headliner_l', 'headliner'), ('seat', 'bolster'), ('seat_accent', 'insert'), ('dial', 'dial')):
        _recolor(M[key], S[col])
    M.update({
        'dash_soft': mat('toon_dash_soft', S['dash'], 0.8),
        'plastic': mat('toon_plastic', tuple(c * 0.85 for c in S['dash']), 0.6),
        'seat_fabric': mat('toon_seat_fabric', S['seat'], 0.95),
        'headliner': mat('toon_headliner', S['headliner'], 0.9),
        'carpet': mat('toon_carpet', S['carpet'], 1.0),
        'stitch': mat('toon_stitch_NoInk', S['stitch'], 0.8),
        'knob': mat('toon_knob', S['knob'], 0.35),
        'tick': mat('toon_tick_NoInk', S['tick'], 0.6),
        'wood': mat('toon_wood', (0.42, 0.22, 0.1), 0.45),
        'wood_grain': mat('toon_wood_grain_NoInk', (0.3, 0.14, 0.06), 0.6),
        'suede': mat('toon_suede', (0.1, 0.1, 0.11), 1.0),
        'alu': mat('toon_alu', (0.62, 0.63, 0.66), 0.35, 0.5),
        'extinguisher': mat('toon_extinguisher', (0.85, 0.1, 0.08), 0.4),
        'harness_red': mat('toon_harness_red', (0.8, 0.08, 0.06), 0.8),
        'plaid_a': mat('toon_plaid_a_NoInk', (0.08, 0.08, 0.12), 0.9),
        'plaid_b': mat('toon_plaid_b_NoInk', (0.85, 0.82, 0.75), 0.9),
        'die': mat('toon_die', (0.96, 0.96, 0.94), 0.8),
        'helmet': mat('toon_helmet', (0.95, 0.95, 0.95), 0.3),
        'thermos': mat('toon_thermos', (0.15, 0.4, 0.25), 0.5, 0.3),
    })


# ------------------------------------------------------------------------------------------------
# cabin

def interior(c, rear_seats=True):
    b, M = c.b, c.M
    sid = getattr(c, 'cabin_style', c.id)  # roster cars borrow a hand-made cabin (D-089)
    ST = STYLES.get(sid, STYLES['D01'])
    box, cyl, torus, tube_along, link, fix_normals = H['box'], H['cyl'], H['torus'], H['tube_along'], H['link'], H['fix_normals']
    st = b.st
    zs = min(s[2] for s in st)
    near = [s for s in st if c.y_rw1 - 0.2 <= s[0] <= c.y_ws0 + 0.2]
    zb = min(s[3] for s in near) if near else st[0][3]
    W = min(s[1] for s in st) - 0.1
    xd = -0.37
    ys = c.y_roof0 - 0.45
    zh_roof = c.top_z(xd, ys - 0.22) - 0.21   # head clearance under the roof
    floor = zs + 0.1
    zc = max(floor + 0.10, zh_roof - HEAD_H)
    zh = zc + HEAD_H
    c.seat = (ys, zc, zh)
    c.floor = floor
    E = Vector((xd, ys - 0.12, zh + 0.03))
    SW = Vector((xd, E.y + 0.55, E.z - 0.36))
    c.wheel_centre = SW
    c.wheel_tilt = 0.42
    G = Vector((xd, SW.y + 0.31, SW.z + 0.14))
    yf = max(c.y_ws0 - 0.02, G.y + 0.36)
    zf = c.top_z(0, yf) - 0.05
    yr = G.y + 0.03
    tr = min(G.z + 0.075, zf)
    c.dash = (yf, yr, tr)
    items = []
    A = items.append

    def extrude_x(name, prof, x0, x1, m):
        n = len(prof)
        verts = [(x0, y, z) for y, z in prof] + [(x1, y, z) for y, z in prof]
        faces = [tuple(range(n))[::-1], tuple(range(n, 2 * n))]
        for k in range(n):
            faces.append((k, (k + 1) % n, n + (k + 1) % n, n + k))
        o = link(name, verts, faces, [m])
        fix_normals(o)
        return o

    # --- shell: carpeted floor with mats, firewall, tunnel, bulkhead, tubs, parcel shelf
    y_back = c.y_rw1 + 0.05
    A(box('floor', (0, (yf + y_back) / 2, floor - 0.02), (2 * W, yf - y_back, 0.04), M['carpet']))
    for sx in (-0.37, 0.37):
        A(box('floor_mat', (sx, yf - 0.32, floor + 0.005), (0.44, 0.5, 0.012), M['mat_floor'], 0.01))
        for k in range(4):
            A(box('mat_rib', (sx, yf - 0.5 + k * 0.1, floor + 0.013), (0.36, 0.015, 0.006), M['carpet']))
    A(box('firewall', (0, yf, (floor + tr) / 2), (2 * W, 0.04, tr - floor), M['carpet']))
    A(box('tunnel', (0, (yf + ys + 0.2) / 2, floor + 0.1), (0.26, yf - ys - 0.2, 0.2), M['carpet'], 0.05, seg=2))
    A(box('rear_bulkhead', (0, y_back + 0.02, (floor + zb) / 2), (2 * W, 0.04, zb - floor), M['plastic']))
    A(box('parcel_shelf', (0, y_back + 0.15, zb - 0.02), (2 * W, 0.3, 0.03), M['carpet']))
    for sgn in (-1, 1):
        A(cyl('shelf_speaker', (sgn * 0.45, y_back + 0.15, zb + 0.0), 0.07, 0.012, M['plastic'], rot=(0, 0, 0), verts=20))
        A(torus('shelf_speaker_ring', (sgn * 0.45, y_back + 0.15, zb + 0.006), 0.07, 0.006, M['chrome'], rot=(0, 0, 0), seg=20, mseg=4))
        if y_back < -c.wb / 2 < ys:
            th = c.r + 0.64 - floor
            A(box('wheel_tub', (sgn * (W - 0.14), -c.wb / 2, floor + th / 2), (0.3, 0.86, th), M['carpet'], 0.08, seg=2))

    # --- doors: card, inset, armrest, pull, handle, lock pin, switches, pocket, speaker, sill
    for sgn in (-1, 1):
        y0, y1 = yf - 0.05, min(ys - 0.35, c.y_rw0 + 0.1)
        ym = (y0 + y1) / 2
        A(box('door_card', (sgn * W, ym, (floor + zb) / 2), (0.05, y0 - y1, zb - floor), M['plastic'], 0.012))
        A(box('door_inset', (sgn * (W - 0.028), ym + 0.05, floor + (zb - floor) * 0.62), (0.012, (y0 - y1) * 0.55, (zb - floor) * 0.3), M['dash_soft'], 0.01))
        A(box('door_stitch', (sgn * (W - 0.036), ym + 0.05, floor + (zb - floor) * 0.78), (0.004, (y0 - y1) * 0.55, 0.004), M['stitch']))
        A(box('door_top', (sgn * (W - 0.02), ym, zb - 0.03), (0.05, y0 - y1, 0.03), M['dash_soft'], 0.01))
        A(box('armrest', (sgn * (W - 0.06), ys + 0.05, zc + 0.2), (0.1, 0.42, 0.06), M['dash_soft'], 0.02, seg=2))
        A(box('door_pull', (sgn * (W - 0.04), ys + 0.3, zc + 0.3), (0.025, 0.16, 0.035), M['plastic'], 0.01))
        A(box('door_handle', (sgn * (W - 0.035), ys + 0.42, zc + 0.36), (0.02, 0.1, 0.025), M['chrome'], 0.008))
        A(box('handle_recess', (sgn * (W - 0.028), ys + 0.42, zc + 0.36), (0.008, 0.13, 0.05), M['dark']))
        A(cyl('lock_pin', (sgn * (W - 0.03), ys + 0.2, zb - 0.005), 0.006, 0.03, M['chrome'], rot=(0, 0, 0), verts=8))
        A(box('door_pocket', (sgn * (W - 0.05), ym + 0.1, floor + 0.12), (0.06, (y0 - y1) * 0.5, 0.1), M['plastic'], 0.01))
        A(box('pocket_opening', (sgn * (W - 0.078), ym + 0.1, floor + 0.165), (0.006, (y0 - y1) * 0.46, 0.012), M['dark']))
        A(cyl('speaker', (sgn * (W - 0.03), yf - 0.25, floor + 0.23), 0.075, 0.02, M['plastic'], rot=(0, math.pi / 2, 0), verts=24))
        A(torus('speaker_ring', (sgn * (W - 0.036), yf - 0.25, floor + 0.23), 0.075, 0.007, M['chrome'], rot=(0, math.pi / 2, 0), seg=24, mseg=4))
        for k in range(4):
            A(box('speaker_slot', (sgn * (W - 0.04), yf - 0.25, floor + 0.2 + k * 0.02), (0.004, 0.1, 0.006), M['dark']))
        A(box('door_sill', (sgn * (W + 0.02), (yf + y_back) / 2, floor + 0.04), (0.08, yf - y_back, 0.05), M['chrome'], 0.01))
        if sgn < 0:  # driver's window/mirror switch pack
            A(box('switch_pack', (sgn * (W - 0.07), ys + 0.18, zc + 0.235), (0.07, 0.12, 0.015), M['plastic'], 0.006))
            for k in range(4):
                A(box('window_switch', (sgn * (W - 0.07) + (k % 2) * 0.03 - 0.015, ys + 0.14 + (k // 2) * 0.05, zc + 0.245), (0.022, 0.03, 0.01), M['dark'], 0.003))
        # grab handle and B-pillar trim (the A-pillars are the body's black pillars)
        zt = c.top_z(sgn * (W - 0.12), ys - 0.1) - 0.08
        A(box('grab_handle', (sgn * (W - 0.1), ys - 0.1, zt), (0.03, 0.2, 0.025), M['plastic'], 0.008))
        bp = ys - 0.42
        A(box('b_pillar_trim', (sgn * (W - 0.02), bp, (zb + c.top_z(sgn * W, bp)) / 2), (0.05, 0.1, c.top_z(sgn * W, bp) - zb), M['headliner'], 0.02))
        A(box('belt_anchor', (sgn * (W - 0.05), bp + 0.02, c.top_z(sgn * W, bp) - 0.14), (0.03, 0.05, 0.05), M['chrome'], 0.01))

    # --- closing the cabin: rear quarter trims behind the doors, kick panels under the dash ends,
    # a sill-to-roof B-pillar lining (no daylight between panels from the cockpit)
    for sgn in (-1, 1):
        y1 = min(ys - 0.35, c.y_rw0 + 0.1)
        if y1 > y_back + 0.02:
            A(box('rear_quarter_trim', (sgn * W, (y1 + y_back) / 2, (floor + zb) / 2), (0.05, y1 - y_back, zb - floor), M['plastic'], 0.012))
            A(box('rear_quarter_top', (sgn * (W - 0.02), (y1 + y_back) / 2, zb - 0.03), (0.05, y1 - y_back, 0.03), M['dash_soft'], 0.01))
        A(box('kick_panel', (sgn * (W - 0.04), yf - 0.12, floor + (tr - 0.36 - floor) / 2 + 0.02), (0.1, 0.26, tr - 0.36 - floor), M['plastic'], 0.01))
        A(box('dash_end_cap', (sgn * (W - 0.01), (yf + yr) / 2, tr - 0.18), (0.04, yf - yr + 0.02, 0.38), M['plastic'], 0.01))
    # --- dashboard with stitched soft top, binnacle, 4 dials, warning lights
    A(extrude_x('dashboard', [(yf, zf), (yr + 0.05, tr), (yr, tr - 0.05), (yr + 0.02, tr - 0.2), (yr + 0.14, tr - 0.36), (yf, tr - 0.36)], -W, W, M['dash_soft']))
    A(extrude_x('dash_pad', [(yf - 0.02, zf + 0.012), (yr + 0.06, tr + 0.012), (yr + 0.06, tr - 0.002), (yf - 0.02, zf - 0.002)], -W + 0.02, W - 0.02, M['plastic']))
    A(box('dash_stitch', (0, yr + 0.07, tr + 0.012), (2 * W - 0.1, 0.004, 0.004), M['stitch']))
    A(box('dash_trim', (0.1, yr + 0.002, tr - 0.1), (2 * W - 0.5, 0.01, 0.03), M['grey'], 0.004))
    # cluster by style (D-070): the four animated needles in every layout, extra gauges as decoration
    bw = {'twin': 0.22, 'wrap': 0.27, 'rally': 0.24, 'chrome': 0.27, 'digital': 0.25}[ST['cluster']]
    digital = ST['cluster'] == 'digital'  # D-081: drawn dials on a screen, glowing rings
    A(extrude_x('binnacle', [(G.y + 0.15, G.z + 0.02), (G.y - 0.04, G.z + 0.11), (G.y - 0.07, G.z + 0.09), (G.y + 0.12, G.z + 0.0)], xd - bw, xd + bw, M['plastic']))
    A(box('cluster_back', (xd, G.y + 0.02, G.z), (2 * bw - 0.04, 0.02, 0.15), M['alu'] if ST['cluster'] == 'rally' else M['dark']))
    dials = {
        'twin': (('NEEDLE_SPEED', -0.09, 0, 0.056), ('NEEDLE_REV', 0.09, 0, 0.056), ('NEEDLE_FUEL', -0.17, -0.035, 0.026), ('NEEDLE_TEMP', 0.17, -0.035, 0.026)),
        'wrap': (('NEEDLE_REV', 0.0, 0.005, 0.062), ('NEEDLE_SPEED', 0.14, -0.01, 0.048), ('NEEDLE_FUEL', -0.13, 0.018, 0.026), ('NEEDLE_TEMP', -0.13, -0.042, 0.026),
                 (None, 0.215, 0.03, 0.022), (None, 0.215, -0.035, 0.022)),
        'rally': (('NEEDLE_REV', 0.0, 0.0, 0.072), ('NEEDLE_SPEED', -0.145, -0.02, 0.04), ('NEEDLE_FUEL', 0.14, 0.022, 0.024), ('NEEDLE_TEMP', 0.14, -0.038, 0.024)),
        'chrome': (('NEEDLE_FUEL', -0.19, 0, 0.034), ('NEEDLE_SPEED', -0.065, 0, 0.058), ('NEEDLE_REV', 0.065, 0, 0.058), ('NEEDLE_TEMP', 0.19, 0, 0.034)),
        'digital': (('NEEDLE_SPEED', -0.1, 0, 0.06), ('NEEDLE_REV', 0.1, 0, 0.06), ('NEEDLE_FUEL', 0.0, 0.035, 0.022), ('NEEDLE_TEMP', 0.0, -0.035, 0.022)),
    }[ST['cluster']]
    if ST['cluster'] == 'chrome':  # a chrome-framed panel across the whole binnacle
        for dz_ in (0.078, -0.078):
            A(box('cluster_frame', (xd, G.y - 0.006, G.z + dz_), (2 * bw - 0.02, 0.012, 0.012), M['chrome'], 0.004))
        for dx_ in (-bw + 0.012, bw - 0.012):
            A(box('cluster_frame', (xd + dx_, G.y - 0.006, G.z), (0.012, 0.012, 0.168), M['chrome'], 0.004))
    for tag, dx, dz, rad in dials:
        cc = Vector((xd + dx, G.y, G.z + dz))
        A(cyl('dial', cc, rad, 0.006, M['dial'], rot=(math.pi / 2, 0, 0), verts=28))
        A(torus('dial_ring', cc + Vector((0, -0.004, 0)), rad, 0.005, M['warn_blue'] if digital else M['chrome'], rot=(math.pi / 2, 0, 0), seg=28, mseg=4))
        n_ticks = 11 if rad > 0.04 else 5
        for k in range(n_ticks):
            a = math.radians(-125 + 250 * k / (n_ticks - 1))
            q = cc + Vector((math.sin(a) * rad * 0.8, -0.005, math.cos(a) * rad * 0.8))
            red = tag == 'NEEDLE_REV' and k >= 9
            t = box('tick', q, (0.003, 0.002, rad * (0.22 if k % 2 == 0 else 0.13)), M['tail'] if red else M['tick'])
            t.rotation_euler = (0, a, 0)
            A(t)
        A(cyl('needle_cap', cc + Vector((0, -0.009, 0)), rad * 0.12, 0.004, M['trim'], rot=(math.pi / 2, 0, 0), verts=10))
        if tag:
            c.animated(tag, [box('needle', cc + Vector((0, -0.007, rad * 0.38)), (0.004, 0.003, rad * 0.76), M['needle'])], cc + Vector((0, -0.007, 0)), (math.pi / 2, 0, 0))
        else:  # a decorative gauge (oil pressure, volts): fixed needle
            A(box('needle', cc + Vector((rad * 0.2, -0.007, rad * 0.3)), (0.004, 0.003, rad * 0.7), M['needle'], rot=(0, 0.6, 0)))
    # warning lamps (D-079): dim lenses, and lit faces the game switches - indicator arrows, handbrake,
    # engine (low health), high beam
    # They sit on the binnacle's sloped front lip (the only cluster surface the wheel never hides from the
    # driver's eye), flanking the shift lights.
    lip_a = math.atan2(0.02, 0.03)
    lip_n = Vector((0, -math.sin(lip_a), math.cos(lip_a)))  # faces the driver and up
    lip = Vector((xd, G.y - 0.055, G.z + 0.1)) + lip_n * 0.003
    c.lip = (lip, lip_n, lip_a)
    lamps = (('WARN_IND_L', 'warn_green', -0.185), ('WARN_HANDBRAKE', 'warn_red', -0.155), ('WARN_ENGINE', 'warn_amber', -0.125),
             ('WARN_BEAM', 'warn_blue', 0.125), ('WARN_HAZARD', 'warn_red', 0.155), ('WARN_IND_R', 'warn_green', 0.185))
    for tag, m, dx in lamps:
        q = lip + Vector((dx * min(1.0, (bw - 0.02) / 0.185), 0, 0))
        A(box('warning_light', q, (0.022, 0.018, 0.004), M['lamp_off'], rot=(lip_a, 0, 0)))
        if tag.startswith('WARN_IND'):  # an arrow
            sgn = -1 if tag.endswith('L') else 1
            lit = [box('warn_arrow', q + lip_n * 0.002, (0.013, 0.006, 0.003), M[m], rot=(lip_a, 0, 0)),
                   box('warn_arrow_head', q + lip_n * 0.002 + Vector((sgn * 0.007, 0, 0)), (0.008, 0.012, 0.003), M[m], rot=(lip_a, 0, 0))]
        else:
            lit = [box('warn_lit', q + lip_n * 0.002, (0.018, 0.013, 0.003), M[m], rot=(lip_a, 0, 0))]
        c.animated(tag, lit, q)
    # centre stack: screen radio with presets and knob, climate knobs, fan buttons, hazard, clock
    cy = yr + 0.06
    A(box('centre_stack', (0, cy + 0.02, tr - 0.2), (0.3, 0.08, 0.36), M['plastic'], 0.02))
    A(box('stack_bezel', (0, cy - 0.021, tr - 0.2), (0.28, 0.004, 0.34), M['grey'], 0.004))
    A(box('radio_screen', (0, cy - 0.024, tr - 0.09), (0.2, 0.006, 0.07), M['screen']))
    if ST.get('tablet'):  # a floating touch screen on the dash top, tilted to the driver (D-081)
        tq = Vector((0.0, yr + 0.06, tr + 0.085))
        A(box('tablet_bezel', tq, (0.3, 0.018, 0.17), M['dark'], 0.008, rot=(-0.18, 0, 0)))
        A(box('tablet_screen', tq + Vector((0, -0.01, 0.002)), (0.28, 0.004, 0.15), M['screen'], rot=(-0.18, 0, 0)))
        A(box('tablet_map', tq + Vector((-0.04, -0.013, 0.004)), (0.17, 0.003, 0.12), M['dial'], rot=(-0.18, 0, 0)))  # night-mode map: dark, the route glows
        A(box('tablet_route', tq + Vector((-0.05, -0.0145, 0.01)), (0.012, 0.003, 0.09), M['warn_amber'], rot=(-0.18, 0, 0.5)))
        for k in range(3):  # media tiles on the right
            A(box('tablet_tile', tq + Vector((0.09, -0.013, 0.04 - k * 0.04)), (0.06, 0.003, 0.03), M['tick'], rot=(-0.18, 0, 0)))
        A(box('tablet_stem', tq + Vector((0, 0.03, -0.09)), (0.06, 0.04, 0.05), M['dark'], 0.01))
    for k in range(6):
        A(box('preset_button', (-0.1 + k * 0.04, cy - 0.024, tr - 0.14), (0.03, 0.008, 0.014), M['dark'], 0.003))
    for sx in (-0.12, 0.12):
        A(cyl('radio_knob', (sx, cy - 0.028, tr - 0.09), 0.016, 0.02, M['chrome'], rot=(math.pi / 2, 0, 0), verts=14))
    for k in range(3):
        A(cyl('hvac_knob', (-0.09 + 0.09 * k, cy - 0.03, tr - 0.25), 0.024, 0.022, M['plastic'], rot=(math.pi / 2, 0, 0), verts=16))
        A(box('hvac_mark', (-0.09 + 0.09 * k, cy - 0.042, tr - 0.237), (0.004, 0.003, 0.012), M['stitch']))
    for k in range(4):
        A(box('fan_button', (-0.06 + k * 0.04, cy - 0.025, tr - 0.31), (0.03, 0.008, 0.014), M['dark'], 0.003))
    hz = Vector((0, cy - 0.026, tr - 0.185))
    c.animated('HAZARD_BUTTON', [box('hazard', hz, (0.04, 0.01, 0.022), M['tail'], 0.004),
                                 box('hazard_tri', hz + Vector((0, -0.006, 0)), (0.016, 0.003, 0.012), M['plastic'], rot=(0, 0.785, 0))],
               hz, (math.pi / 2, 0, 0))
    A(box('clock', (0, cy - 0.024, tr - 0.035), (0.06, 0.006, 0.022), M['warn_green']))
    for vx in (-0.075, 0.075, -W + 0.15, W - 0.15):
        vy = cy - 0.02 if abs(vx) < 0.1 else yr + 0.01
        A(box('vent', (vx, vy, tr - 0.04), (0.12, 0.022, 0.055), M['dark'], 0.005))
        for k in range(4):
            A(box('vent_slat', (vx, vy - 0.013, tr - 0.06 + 0.014 * k), (0.11, 0.004, 0.004), M['grey']))
        A(cyl('vent_knob', (vx, vy - 0.016, tr - 0.04), 0.007, 0.01, M['chrome'], rot=(math.pi / 2, 0, 0), verts=8))
    A(box('glovebox_line', (0.37, yr + 0.012, tr - 0.14), (0.36, 0.004, 0.004), M['line']))
    A(box('glovebox_line2', (0.37, yr + 0.03, tr - 0.29), (0.36, 0.004, 0.004), M['line']))
    A(box('glovebox_latch', (0.37, yr + 0.008, tr - 0.11), (0.06, 0.008, 0.014), M['chrome'], 0.004))
    A(box('airbag_seam', (0.37, yr + 0.06, tr + 0.012), (0.3, 0.12, 0.003), M['dark']))

    # --- steering column: shroud, key with fob, indicator and wiper stalks (animated)
    cd = Vector((0, math.cos(c.wheel_tilt), -math.sin(c.wheel_tilt) * 0.6)).normalized()
    A(tube_along('column', [SW + cd * 0.05, SW + cd * 0.32], 0.03, M['plastic'], sides=10))
    A(box('column_shroud', SW + cd * 0.17 + Vector((0, 0, -0.02)), (0.11, 0.2, 0.09), M['plastic'], 0.025, seg=2))
    kp = SW + cd * 0.2 + Vector((0.07, 0, -0.03))
    A(box('ignition_key', kp, (0.03, 0.012, 0.008), M['chrome'], 0.003))
    A(tube_along('key_chain', [kp + Vector((0.015, 0, 0)), kp + Vector((0.02, 0, -0.03)), kp + Vector((0.018, 0.005, -0.05))], 0.002, M['chrome'], sides=5))
    A(box('key_fob', kp + Vector((0.018, 0.006, -0.065)), (0.02, 0.01, 0.03), M['accent'], 0.005))
    for tag, sgn in (('STALK_L', -1), ('STALK_R', 1)):
        root = SW + cd * 0.12 + Vector((sgn * 0.05, 0, 0.0))
        c.animated(tag, [tube_along('stalk', [root, root + Vector((sgn * 0.13, -0.02, 0.01))], 0.006, M['plastic'], sides=6),
                         cyl('stalk_tip', root + Vector((sgn * 0.14, -0.022, 0.011)), 0.01, 0.03, M['plastic'], rot=(0, math.pi / 2, 0), verts=8)],
                   root, (0, math.pi / 2, 0))

    # --- steering wheel: leather rim with thumb grips, 12 o'clock stripe, 3 shaped spokes, horn pad
    R = 0.18
    tilt = (math.pi / 2 - c.wheel_tilt, 0, 0)
    up_w = Vector((0, math.sin(c.wheel_tilt), math.cos(c.wheel_tilt)))           # wheel "12 o'clock" direction
    ax = Vector((0, -math.cos(c.wheel_tilt), math.sin(c.wheel_tilt)))            # towards the driver
    # the wheel by style (D-070); the rim stays at R so the hands' grip is the same in every car
    wst = ST['wheel']

    def rim_dir(a):
        return Vector((math.cos(a), 0, 0)) + up_w * math.sin(a)
    rim_m = M['suede'] if wst == 'dish' else M['trim']
    wheel = [torus('steering_wheel', SW, R, {'classic': 0.012, 'dish': 0.02}.get(wst, 0.019), rim_m, rot=tilt, seg=48, mseg=10)]
    if wst != 'classic':
        for sgn in (-1, 1):
            for k in range(5):  # thumb-grip bulge at 9 and 3
                a = (k - 2) * 0.09
                wheel.append(_sphere('grip_bulge', SW + Vector((sgn * math.cos(a) * R, 0, 0)) + up_w * math.sin(a) * R, 0.024, rim_m, seg=10, rings=6))
        wheel.append(box('rim_stripe', SW + up_w * R, (0.018, 0.045, 0.045), M['warn_amber'] if wst == 'dish' else M['stitch'], 0.004, rot=(-c.wheel_tilt, 0, 0)))
    dish = 0.065 if wst == 'dish' else 0.012
    hub = SW + ax * dish
    angles = {'three_spoke': (0.0, math.pi, -math.pi / 2), 'dish': (0.0, math.pi, -math.pi / 2),
              'four_spoke': (-0.25, math.pi + 0.25, -math.pi / 2 - 0.6, -math.pi / 2 + 0.6),
              'classic': (-0.3, math.pi + 0.3, -math.pi / 2 - 0.5, -math.pi / 2 + 0.5)}[wst]
    spoke_m = M['chrome'] if wst == 'classic' else (M['alu'] if wst == 'dish' else M['plastic'])
    spoke_r = {'classic': 0.007, 'dish': 0.011, 'four_spoke': 0.012}.get(wst, 0.013)
    for a in angles:
        d = rim_dir(a)
        pts = [hub + d * 0.045, SW + d * 0.11 + ax * dish * 0.45, SW + d * (R - 0.012)]
        wheel.append(tube_along('spoke', pts, spoke_r, spoke_m, sides=8))
        if wst == 'dish':  # drilled spokes
            for k in range(2):
                wheel.append(cyl('spoke_hole', SW + d * (0.08 + 0.035 * k) + ax * (dish * 0.55 + 0.012), 0.006, 0.004, M['dark'], rot=tilt, verts=8))
    if wst == 'three_spoke':
        wheel.append(_sphere('horn_pad', SW + ax * 0.018, 0.058, M['trim'], scale=(1.3, 0.55, 1.0)))
        wheel[-1].rotation_euler = (-c.wheel_tilt, 0, 0)
    elif wst == 'four_spoke':  # a wide padded bar across the hub
        wheel.append(box('horn_pad', hub + ax * 0.012, (0.13, 0.05, 0.075), M['trim'], 0.02, rot=(-c.wheel_tilt, 0, 0), seg=2))
    elif wst == 'dish':
        wheel.append(cyl('horn_pad', hub + ax * 0.008, 0.042, 0.02, M['trim'], rot=tilt, verts=20))
    else:  # classic: a small chrome cap and a thin chrome horn ring inside the rim
        wheel.append(cyl('horn_pad', hub + ax * 0.01, 0.035, 0.018, M['chrome'], rot=tilt, verts=20))
        wheel.append(torus('horn_ring', hub + ax * 0.006, 0.078, 0.005, M['chrome'], rot=tilt, seg=40, mseg=6))
    wheel.append(cyl('sw_badge', hub + ax * 0.03, 0.018, 0.006, M['accent'], rot=tilt, verts=16))
    c.animated('STEER_PIVOT', wheel, SW, tilt)

    # --- pedals on hinges (animated): clutch, brake, gas; a dead pedal
    ph = floor + 0.36
    for tag, dx, wdt, hgt in (('PEDAL_CLUTCH', -0.13, 0.065, 0.075), ('PEDAL_BRAKE', 0.0, 0.08, 0.075), ('PEDAL_GAS', 0.13, 0.05, 0.14)):
        hinge = Vector((xd + dx, yf - 0.04, ph))
        pad = hinge + Vector((0, -0.1, -0.2))
        parts = [tube_along('pedal_arm', [hinge, hinge + Vector((0, -0.05, -0.12)), pad], 0.008, M['grey'], sides=6),
                 box('pedal_pad', pad, (wdt, 0.015, hgt), M['grey'], 0.005, rot=(0.45, 0, 0))]
        for k in range(3):
            parts.append(cyl('pedal_hole', pad + Vector((0, -0.009, -hgt * 0.3 + k * hgt * 0.3)), 0.007, 0.004, M['dark'], rot=(math.pi / 2 - 0.45, 0, 0), verts=8))
        c.animated(tag, parts, hinge, (0, math.pi / 2, 0))
        if tag in ('PEDAL_GAS', 'PEDAL_CLUTCH'):  # the ankle when the foot rests on the pad
            ft = H['bpy'].data.objects.new('FOOT_TARGET_' + ('R' if tag == 'PEDAL_GAS' else 'L'), None)
            H['bpy'].context.scene.collection.objects.link(ft)
            ft.location = pad + Vector((0, -0.16, 0.07))
            c.foot_targets = getattr(c, 'foot_targets', []) + [(ft, tag)]
    A(box('dead_pedal', (xd - 0.25, yf - 0.16, floor + 0.12), (0.07, 0.02, 0.14), M['grey'], 0.005, rot=(0.6, 0, 0)))

    # --- seats: bucket shells with bolsters, stitching, harness slots, rails, recline lever
    for sgn in (-1, 1):
        x = sgn * 0.37
        A(box('seat_rail', (x, ys + 0.05, floor + 0.03), (0.34, 0.5, 0.04), M['grey']))
        A(box('seat_base', (x, ys + 0.02, (zc + floor) / 2), (0.36, 0.42, max(0.03, zc - floor - 0.05)), M['plastic']))
        A(box('seat_cushion', (x, ys + 0.02, zc), (0.44, 0.5, 0.1), M['seat_fabric'], 0.04, seg=2))
        A(box('cushion_insert', (x, ys + 0.02, zc + 0.052), (0.22, 0.42, 0.006), M['seat_accent']))
        for bx in (-1, 1):
            A(box('cushion_bolster', (x + bx * 0.2, ys + 0.02, zc + 0.05), (0.08, 0.48, 0.12), M['seat'], 0.035, seg=2))
            A(box('back_bolster', (x + bx * 0.21, ys - 0.25, zc + 0.36), (0.09, 0.14, 0.62), M['seat'], 0.04, seg=2, rot=(-0.22, 0, bx * 0.15)))
            A(box('cushion_stitch', (x + bx * 0.112, ys + 0.02, zc + 0.056), (0.004, 0.42, 0.004), M['stitch']))
        A(box('seat_back', (x, ys - 0.28, zc + 0.36), (0.42, 0.1, 0.68), M['seat_fabric'], 0.04, seg=2, rot=(-0.22, 0, 0)))
        A(box('seat_centre', (x, ys - 0.225, zc + 0.34), (0.22, 0.02, 0.5), M['seat_accent'], 0.01, rot=(-0.22, 0, 0)))
        for bx in (-1, 1):
            A(box('back_stitch', (x + bx * 0.112, ys - 0.215, zc + 0.34), (0.004, 0.012, 0.48), M['stitch'], rot=(-0.22, 0, 0)))
            A(box('harness_slot', (x + bx * 0.09, ys - 0.3, zc + 0.62), (0.05, 0.02, 0.015), M['dark'], rot=(-0.22, 0, 0)))
        A(box('headrest', (x, ys - 0.37, zh - 0.02), (0.26, 0.09, 0.18), M['seat'], 0.035, seg=2, rot=(-0.22, 0, 0)))
        A(box('recline_lever', (x + sgn * 0.24, ys - 0.1, zc - 0.02), (0.02, 0.12, 0.025), M['plastic'], 0.006))
        if sgn > 0:
            A(box('seat_belt', (x - 0.02, ys - 0.2, zc + 0.35), (0.05, 0.012, 0.62), M['dark'], rot=(-0.22, 0.55, 0)))
            A(box('belt_buckle', (x - 0.24, ys + 0.05, zc + 0.08), (0.03, 0.05, 0.06), M['chrome'], 0.008))

    # --- console: gaiter + lever (animated H-pattern), cup holders, handbrake (animated), cubby
    A(box('console', (0, ys + 0.12, floor + 0.22), (0.22, 0.62, 0.2), M['plastic'], 0.03, seg=2))
    A(box('console_top', (0, ys + 0.12, floor + 0.325), (0.2, 0.58, 0.012), M['dash_soft'], 0.006))
    gb = Vector((0, ys + 0.38, floor + 0.34))
    A(_cone('gaiter', gb + Vector((0, 0, 0.04)), 0.065, 0.02, 0.08, M['dark'], verts=14))
    A(torus('gaiter_ring', gb + Vector((0, 0, 0.002)), 0.066, 0.006, M['chrome'], rot=(0, 0, 0), seg=18, mseg=4))
    L = 0.19
    knob = gb + Vector((0, 0, L))
    lever = [H['cyl']('shifter', gb + Vector((0, 0, L / 2)), 0.009, L, M['chrome'], rot=(0, 0, 0), verts=8),
             _sphere('shift_knob', knob, 0.03, M['knob'], scale=(1, 1, 0.9), seg=16, rings=8),
             box('shift_pattern', knob + Vector((0, 0, 0.026)), (0.022, 0.022, 0.003), M['stitch'])]
    c.animated('GEAR_LEVER', lever, gb)
    ke = H['bpy'].data.objects.new('GEAR_KNOB', None)
    H['bpy'].context.scene.collection.objects.link(ke)
    ke.location = knob
    c.knob_empty = ke
    for k in range(2):
        A(cyl('cup_holder', (0, ys + 0.2 - k * 0.1, floor + 0.315), 0.04, 0.03, M['dark'], rot=(0, 0, 0), verts=16))
    hb = Vector((0.12, ys - 0.14, floor + 0.34))
    c.animated('HANDBRAKE_LEVER', [box('handbrake', hb + Vector((0, 0.13, 0.02)), (0.04, 0.26, 0.04), M['plastic'], 0.012, rot=(0.12, 0, 0)),
                                   box('handbrake_grip', hb + Vector((0, 0.2, 0.03)), (0.046, 0.12, 0.046), M['trim'], 0.012, rot=(0.12, 0, 0)),
                                   box('handbrake_button', hb + Vector((0, 0.27, 0.04)), (0.02, 0.02, 0.02), M['chrome'])],
               hb, (0, math.pi / 2, 0))
    hg = H['bpy'].data.objects.new('HANDBRAKE_GRIP', None)  # where the driver's hand pulls (D-078)
    H['bpy'].context.scene.collection.objects.link(hg)
    hg.location = hb + Vector((0, 0.2, 0.03))
    c.foot_targets = getattr(c, 'foot_targets', []) + [(hg, 'HANDBRAKE_LEVER')]  # parented to the lever like the foot targets
    A(box('handbrake_boot', hb + Vector((0, 0.05, -0.005)), (0.07, 0.12, 0.03), M['dark'], 0.01))
    A(box('cubby_lid', (0, ys - 0.1, floor + 0.34), (0.2, 0.2, 0.02), M['dash_soft'], 0.008))

    # --- rear seats
    if rear_seats:
        yr2 = ys - 0.85
        zr = max(floor + 0.14, c.top_z(0, yr2 - 0.2) - 1.0)
        A(box('rear_bench', (0, yr2, zr), (2 * W - 0.25, 0.46, 0.12), M['seat_fabric'], 0.04, seg=2))
        A(box('rear_back', (0, yr2 - 0.22, zr + 0.3), (2 * W - 0.25, 0.12, 0.52), M['seat_fabric'], 0.04, seg=2, rot=(-0.25, 0, 0)))
        for sgn in (-1, 1):
            A(box('rear_headrest', (sgn * 0.35, yr2 - 0.3, zr + 0.62), (0.24, 0.08, 0.14), M['seat'], 0.03, rot=(-0.25, 0, 0)))
            A(box('rear_insert', (sgn * 0.35, yr2 - 0.16, zr + 0.3), (0.3, 0.012, 0.4), M['seat_accent'], rot=(-0.25, 0, 0)))
            A(box('rear_buckle', (sgn * 0.08, yr2 - 0.05, zr + 0.08), (0.025, 0.04, 0.05), M['chrome'], 0.006))

    # --- closing the gaps (owner: "there are gaps in places"): console ramp up to the centre stack,
    # knee panels under the dash, sail panels with tweeters, lower stack; then life details
    y_ramp0, y_ramp1 = ys + 0.46, yr + 0.1
    z_ramp0, z_ramp1 = floor + 0.33, tr - 0.37

    def ramp_z(y):
        t = max(0.0, min(1.0, (y - y_ramp0) / (y_ramp1 - y_ramp0)))
        return z_ramp0 + (z_ramp1 - z_ramp0) * t
    c.ramp_z = ramp_z
    A(extrude_x('console_ramp', [(y_ramp0, z_ramp0), (y_ramp1, z_ramp1), (y_ramp1, floor), (y_ramp0, floor)], -0.12, 0.12, M['plastic']))
    A(extrude_x('ramp_top', [(y_ramp0, z_ramp0 + 0.006), (y_ramp1, z_ramp1 + 0.006), (y_ramp1, z_ramp1 - 0.004), (y_ramp0, z_ramp0 - 0.004)], -0.1, 0.1, M['dash_soft']))
    for sgn in (-1, 1):
        A(tube_along('ramp_stitch', [Vector((sgn * 0.1, y_ramp0, z_ramp0 + 0.008)), Vector((sgn * 0.1, y_ramp1, z_ramp1 + 0.008))], 0.0025, M['stitch'], sides=4))
        A(box('console_side', (sgn * 0.115, ys + 0.12, floor + 0.2), (0.012, 0.6, 0.22), M['dash_soft'], 0.006))
        A(box('console_knee_pad', (sgn * 0.118, ys + 0.3, floor + 0.22), (0.01, 0.2, 0.1), M['seat'], 0.02))
    # 12 V socket, USB and a coin tray on the ramp
    for k, dx in enumerate((-0.05, 0.05)):
        q = Vector((dx, y_ramp0 + 0.07, ramp_z(y_ramp0 + 0.07) + 0.004))
        A(cyl('socket', q, 0.012, 0.01, M['chrome'], rot=(-0.35, 0, 0), verts=12))
        A(cyl('socket_hole', q + Vector((0, 0.001, 0.005)), 0.008, 0.004, M['dark'], rot=(-0.35, 0, 0), verts=12))
    # knee panels under the dash (driver: bonnet release, fuse lid; passenger under the glovebox)
    kz = tr - 0.36
    for x0, x1 in ((-W + 0.04, -0.13), (0.13, W - 0.04)):
        A(extrude_x('knee_panel', [(yr + 0.14, kz + 0.005), (yr + 0.1, kz - 0.1), (yr + 0.3, kz - 0.14), (yf - 0.02, kz - 0.14), (yf - 0.02, kz + 0.005)], x0, x1, M['plastic']))
    A(box('fuse_lid_line', (xd - 0.2, yr + 0.118, kz - 0.05), (0.12, 0.004, 0.004), M['line']))
    A(box('bonnet_release', (xd - 0.28, yr + 0.105, kz - 0.08), (0.05, 0.02, 0.025), M['warn_amber'], 0.006))
    A(box('stack_lower', (0, yr + 0.12, (kz + z_ramp1) / 2 + 0.01), (0.26, 0.1, kz - z_ramp1 + 0.06), M['plastic'], 0.012))
    A(box('ashtray_lid', (0, yr + 0.066, kz - 0.02), (0.16, 0.006, 0.04), M['grey'], 0.004))
    for sgn in (-1, 1):  # sail panels with tweeters close the corner between the door top and the dash
        sp = Vector((sgn * (W - 0.035), yf - 0.06, zb + 0.03))
        A(box('sail_panel', sp, (0.05, 0.14, 0.12), M['plastic'], 0.012))
        A(_sphere('tweeter', sp + Vector((-sgn * 0.026, -0.01, 0.005)), 0.02, M['dark'], scale=(0.5, 1, 1), seg=12, rings=6))
        A(torus('tweeter_ring', sp + Vector((-sgn * 0.026, -0.01, 0.005)), 0.021, 0.004, M['chrome'], rot=(0, math.pi / 2, 0), seg=16, mseg=4))
        # door card: accent trim line, speaker grille dots, lock knob, bottle in the pocket
        y0d, y1d = yf - 0.05, min(ys - 0.35, c.y_rw0 + 0.1)
        A(box('door_trim_line', (sgn * (W - 0.03), (y0d + y1d) / 2, zc + 0.26), (0.006, y0d - y1d - 0.08, 0.008), M['accent']))
        A(box('door_lower_panel', (sgn * (W - 0.03), (y0d + y1d) / 2, floor + 0.06), (0.012, y0d - y1d - 0.04, 0.08), M['carpet'], 0.006))
        for k in range(10):
            a = k * 2 * math.pi / 10
            A(box('grille_dot', (sgn * (W - 0.047), yf - 0.25 + math.cos(a) * 0.045, floor + 0.23 + math.sin(a) * 0.045), (0.004, 0.01, 0.01), M['grey']))
        A(cyl('lock_knob', (sgn * (W - 0.04), ys + 0.5, zb + 0.005), 0.009, 0.03, M['plastic'], rot=(0, 0, 0), verts=8))
        if sgn > 0:
            bt = Vector((sgn * (W - 0.055), ys + 0.02, floor + 0.2))
            A(cyl('bottle', bt, 0.03, 0.18, M['glass_int'], rot=(math.pi / 2, 0, 0), verts=12))
            A(cyl('bottle_cap', bt + Vector((0, 0.1, 0)), 0.014, 0.025, M['accent'], rot=(math.pi / 2, 0, 0), verts=10))
    # dash top: defroster slots along the screen, a tuner gauge pod (boost / oil / volts)
    for k in range(9):
        A(box('defrost_slot', (-0.64 + k * 0.16, yf - 0.06, zf + 0.004), (0.1, 0.012, 0.004), M['dark']))
    def dash_top(y):  # height of the dash pad at y (between its rear edge and the windscreen)
        t = max(0.0, min(1.0, (y - (yr + 0.06)) / (yf - 0.02 - (yr + 0.06))))
        return tr + 0.012 + (zf - tr) * t
    gp = Vector((0.02, yr + 0.12, dash_top(yr + 0.12) + 0.028))
    A(box('gauge_pod', gp, (0.23, 0.07, 0.07), M['plastic'], 0.02, rot=(0.35, 0, 0)))
    for k, (tag, col) in enumerate((('boost', 'needle'), ('oil', 'needle'), ('volt', 'needle'))):
        dc = gp + Vector((-0.07 + k * 0.07, -0.037, 0.005))
        A(cyl('pod_dial', dc, 0.027, 0.006, M['dial'], rot=(math.pi / 2 - 0.35, 0, 0), verts=20))
        A(torus('pod_ring', dc + Vector((0, -0.003, 0)), 0.027, 0.004, M['chrome'], rot=(math.pi / 2 - 0.35, 0, 0), seg=20, mseg=4))
        A(box('pod_needle', dc + Vector((0.006, -0.006, 0.008)), (0.003, 0.002, 0.02), M[col], rot=(-0.35, -0.6 + 0.5 * k, 0)))
    # headliner: seams, a sunroof frame, a rear hoop and harness bar (half cage)
    yl = c.y_roof0 - 0.35
    zl = c.top_z(0, yl) - 0.035
    A(box('sunroof_frame', (0, yl, zl), (0.62, 0.42, 0.012), M['plastic'], 0.01))
    A(box('sunroof_shade', (0, yl, zl - 0.004), (0.54, 0.34, 0.006), M['headliner']))
    for k in range(3):
        A(box('shade_rib', (0, yl - 0.12 + k * 0.12, zl - 0.008), (0.52, 0.004, 0.003), M['line']))
    A(box('sunroof_handle', (0, yl + 0.2, zl - 0.012), (0.08, 0.02, 0.012), M['chrome'], 0.004))
    for k, yy in enumerate((c.y_roof0 - 0.15, yl - 0.3)):
        A(box('headliner_seam', (0, yy, c.top_z(0, yy) - 0.04), (2 * W - 0.2, 0.004, 0.004), M['line']))
    hy = ys - 0.5
    ztop = c.top_z(0, hy) - 0.08
    hoop = [Vector((-W + 0.1, hy, floor + 0.02)), Vector((-W + 0.12, hy, ztop - 0.12)), Vector((-W + 0.3, hy, ztop)),
            Vector((W - 0.3, hy, ztop)), Vector((W - 0.12, hy, ztop - 0.12)), Vector((W - 0.1, hy, floor + 0.02))]
    A(tube_along('roll_hoop', hoop, 0.02, M['cage'], sides=10))
    A(tube_along('harness_bar', [Vector((-W + 0.12, hy, zh - 0.27)), Vector((W - 0.12, hy, zh - 0.27))], 0.017, M['cage'], sides=10))
    for sgn in (-1, 1):
        A(tube_along('hoop_stay', [Vector((sgn * (W - 0.3), hy, ztop)), Vector((sgn * (W - 0.3), hy - 0.55, zb + 0.02))], 0.016, M['cage'], sides=8))
        A(box('hoop_foot', (sgn * (W - 0.1), hy, floor + 0.01), (0.08, 0.08, 0.012), M['chrome'], 0.004))
    # life: a coffee cup in the holder, a bag on the passenger seat, a map in the door pocket
    cp = Vector((0, ys + 0.2, floor + 0.36))
    A(_cone('coffee_cup', cp, 0.03, 0.036, 0.1, M['cup'], verts=14))
    A(cyl('cup_lid', cp + Vector((0, 0, 0.055)), 0.038, 0.012, M['dark'], rot=(0, 0, 0), verts=14))
    A(box('cup_sleeve', cp + Vector((0, 0, 0.0)), (0.075, 0.075, 0.035), M['accent'], 0.02))
    A(box('sports_bag', (0.37, ys + 0.02, zc + 0.14), (0.32, 0.22, 0.16), M['bag'], 0.06, seg=2))
    A(box('bag_stripe', (0.37, ys + 0.02, zc + 0.14), (0.325, 0.03, 0.165), M['accent'], 0.02))
    A(tube_along('bag_strap', [Vector((0.24, ys + 0.02, zc + 0.2)), Vector((0.37, ys + 0.05, zc + 0.3)), Vector((0.5, ys + 0.02, zc + 0.2))], 0.008, M['dark'], sides=5))
    A(box('door_map', (W - 0.06, (yf - 0.05 + min(ys - 0.35, c.y_rw0 + 0.1)) / 2 + 0.12, floor + 0.2), (0.012, 0.14, 0.09), M['map'], rot=(0.15, 0, 0)))

    # overhead console between the visors: map lights, switches, sunglasses flap
    oc = Vector((0, c.y_roof0 - 0.1, c.top_z(0, c.y_roof0 - 0.1) - 0.035))
    A(box('overhead_console', oc, (0.2, 0.16, 0.035), M['plastic'], 0.012))
    for sgn in (-1, 1):
        A(box('map_light', oc + Vector((sgn * 0.06, 0.03, -0.019)), (0.05, 0.04, 0.004), M['reflector']))
    for k in range(3):
        A(box('roof_switch', oc + Vector((-0.03 + k * 0.03, -0.035, -0.02)), (0.02, 0.025, 0.006), M['dark'], 0.003))
    A(box('glasses_flap', oc + Vector((0, -0.07, -0.012)), (0.12, 0.012, 0.02), M['grey'], 0.004))
    # shift-light LED strip on top of the binnacle (green, amber, red) and a stopwatch on the dash
    has_leds = 'shift_led' not in ST.get('drop', ())
    for k in range(10):
        m = 'warn_green' if k < 4 else ('warn_amber' if k < 7 else 'warn_red')
        lip, lip_n, lip_a = c.lip
        q = lip + Vector((-0.095 + k * 0.021, 0, 0))
        A(box('shift_led', q, (0.015, 0.018, 0.004), M['lamp_off'], 0.002, rot=(lip_a, 0, 0)))
        if has_leds:  # lit with the revs (D-079), on the binnacle lip in the driver's eyeline
            c.animated('SHIFT_LED_%d' % k, [box('shift_led_lit', q + lip_n * 0.002, (0.013, 0.015, 0.003), M[m], rot=(lip_a, 0, 0))], q)
    A(box('led_strip', (xd, G.y - 0.012, G.z + 0.1), (0.22, 0.02, 0.012), M['dark'], 0.004))
    sw_c = Vector((0.36, yr + 0.14, dash_top(yr + 0.14) + 0.026))
    A(cyl('stopwatch', sw_c, 0.028, 0.012, M['dial'], rot=(math.pi / 2 - 0.3, 0, 0), verts=18))
    A(torus('stopwatch_ring', sw_c, 0.028, 0.005, M['chrome'], rot=(math.pi / 2 - 0.3, 0, 0), seg=18, mseg=4))
    A(cyl('stopwatch_crown', sw_c + Vector((0, 0.0, 0.033)), 0.006, 0.01, M['chrome'], rot=(0, 0, 0), verts=8))

    # --- controls the driver reaches for (D-064): each is a pivot the game presses
    ls = Vector((xd - 0.27, yr + 0.004, tr - 0.12))                          # lights knob left of the wheel
    A(cyl('light_knob_ring', ls + Vector((0, 0.004, 0)), 0.03, 0.006, M['grey'], rot=(math.pi / 2, 0, 0), verts=16))
    c.animated('LIGHT_KNOB', [cyl('light_knob', ls, 0.022, 0.03, M['plastic'], rot=(math.pi / 2, 0, 0), verts=14),
                              box('light_knob_mark', ls + Vector((0, -0.016, 0.012)), (0.004, 0.004, 0.01), M['stitch'])], ls, (math.pi / 2, 0, 0))
    rb = Vector((-0.02, cy - 0.028, tr - 0.14))                                 # radio "next station" button
    c.animated('RADIO_BUTTON', [box('radio_next', rb, (0.032, 0.012, 0.016), M['warn_green'], 0.003)], rb, (math.pi / 2, 0, 0))
    # weapons panel on the console ahead of the gear lever: a flip-up cover over a red button
    wp = Vector((0, ys + 0.6, ramp_z(ys + 0.6) + 0.018))
    A(box('weapon_panel', wp, (0.16, 0.12, 0.03), M['dark'], 0.008, rot=(-0.35, 0, 0)))
    for k in range(6):
        A(box('hazard_stripe', wp + Vector((-0.065 + k * 0.026, -0.05, 0.02)), (0.012, 0.012, 0.004), M['warn_amber'] if k % 2 else M['dark'], rot=(-0.35, 0.6, 0)))
    c.animated('WEAPON_BUTTON', [cyl('weapon_button', wp + Vector((0, 0.005, 0.022)), 0.022, 0.018, M['tail'], rot=(-0.35, 0, 0), verts=18)], wp + Vector((0, 0.005, 0.022)), (-0.35, 0, 0))
    c.animated('WEAPON_COVER', [box('weapon_cover', wp + Vector((0, 0.03, 0.04)), (0.06, 0.06, 0.004), M['lens'], 0.002, rot=(-0.35, 0, 0))], wp + Vector((0, -0.02, 0.04)), (0, math.pi / 2, 0))
    # phone on a vent clip with a glowing map, a dash cam behind the mirror (small life details)
    ph = Vector((0.2, yr - 0.005, tr - 0.06))
    A(box('phone_clip', ph + Vector((0, 0.02, -0.03)), (0.03, 0.03, 0.02), M['dark']))
    A(box('phone', ph, (0.075, 0.01, 0.14), M['dark'], 0.008, rot=(0.25, 0, 0)))
    A(box('phone_screen', ph + Vector((0, -0.006, 0.0)), (0.065, 0.003, 0.125), M['screen'], rot=(0.25, 0, 0)))
    A(box('phone_route', ph + Vector((0.01, -0.008, 0.01)), (0.008, 0.002, 0.08), M['warn_amber'], rot=(0.25, 0, 0.3)))
    # --- roof: mirror with day/night tab, visors with vanity mirror, dome light, air freshener
    zin = c.top_z(0, c.y_roof0 - 0.02) - 0.04
    mp = Vector((0, c.y_roof0 - 0.02, zin - 0.1))
    A(box('int_mirror', mp, (0.3, 0.034, 0.085), M['plastic'], 0.016))
    A(box('int_mirror_bezel', mp + Vector((0, -0.018, 0)), (0.296, 0.006, 0.08), M['trim'], 0.006))
    A(H['mirror_quad']('MIRROR_C', mp + Vector((0, -0.022, 0)), 0.28, 0.066, M['mirror_c'], c))  # live rear view in the game
    A(box('dash_cam', mp + Vector((0.13, 0.03, 0.01)), (0.05, 0.05, 0.035), M['dark'], 0.008))
    A(box('mirror_tab', mp + Vector((0, 0.01, -0.048)), (0.03, 0.012, 0.015), M['plastic']))
    A(cyl('int_mirror_stalk', mp + Vector((0, 0, 0.055)), 0.008, 0.08, M['plastic'], rot=(0, 0, 0), verts=6))
    fr = mp + Vector((0.06, 0.005, -0.035))
    tree = [tube_along('freshener_string', [fr, fr + Vector((0, 0, -0.06))], 0.0015, M['shirt'], sides=4)]
    for k, (wdt, z) in enumerate(((0.05, -0.075), (0.04, -0.1), (0.03, -0.12))):
        tree.append(box('freshener_tree', fr + Vector((0, 0, z)), (wdt, 0.003, 0.03), M['freshener'], 0.003))
    tree.append(box('freshener_trunk', fr + Vector((0, 0, -0.14)), (0.01, 0.003, 0.016), M['hair']))
    if ST.get('dice'):
        for o in tree:
            H['bpy'].data.objects.remove(o)
        tree = [tube_along('dice_string', [fr, fr + Vector((-0.02, 0, -0.05))], 0.0015, M['shirt'], sides=4),
                tube_along('dice_string', [fr, fr + Vector((0.02, 0, -0.06))], 0.0015, M['shirt'], sides=4)]
        for k, dx in enumerate((-0.02, 0.02)):
            dcn = fr + Vector((dx, 0, -0.072 - 0.01 * k))
            tree.append(box('fuzzy_die', dcn, (0.036, 0.036, 0.036), M['die'], 0.01))
            for px, pz in ((-0.009, 0.009), (0.0, 0.0), (0.009, -0.009))[:2 + k]:
                tree.append(cyl('die_pip', dcn + Vector((px, -0.019, pz)), 0.004, 0.003, M['dark'], rot=(math.pi / 2, 0, 0), verts=8))
    c.animated('FRESHENER', tree, fr, (math.pi / 2, 0, 0))
    for sgn in (-1, 1):
        vp = Vector((sgn * 0.36, c.y_roof0 - 0.08, c.top_z(sgn * 0.36, c.y_roof0 - 0.08) - 0.055))
        visor = [box('sun_visor', vp, (0.38, 0.16, 0.016), M['headliner'], 0.006),
                 box('visor_mirror', vp + Vector((0, 0, -0.01)), (0.14, 0.07, 0.003), M['chrome']),
                 box('visor_strap', vp + Vector((sgn * 0.12, 0.0, -0.009)), (0.02, 0.14, 0.003), M['dark'])]
        A(box('visor_clip', vp + Vector((-sgn * 0.2, 0.04, 0)), (0.02, 0.03, 0.02), M['plastic']))
        if sgn < 0:  # the driver's visor flips down on its front hinge (D-079)
            c.animated('SUN_VISOR_L', visor, vp + Vector((0, 0.08, 0.004)), (0, math.pi / 2, 0))
        else:
            items.extend(visor)
    dl = Vector((0, c.y_roof0 - 0.5, c.top_z(0, c.y_roof0 - 0.5) - 0.045))
    A(box('dome_light', dl, (0.16, 0.09, 0.02), M['plastic'], 0.008))
    A(box('dome_lens', dl + Vector((0, 0, -0.011)), (0.12, 0.06, 0.004), M['reflector']))
    c.animated('DOME_LIT', [box('dome_lit', dl + Vector((0, 0, -0.0135)), (0.118, 0.058, 0.002), M['dome_on'])], dl)
    dsw = dl + Vector((0.07, -0.05, -0.012))
    c.animated('DOME_SWITCH', [box('dome_switch', dsw, (0.022, 0.012, 0.008), M['dark'], 0.003)], dsw, (math.pi / 2, 0, 0))
    # --- style extras (D-070) -------------------------------------------------------------------
    y0d, y1d = yf - 0.05, min(ys - 0.35, c.y_rw0 + 0.1)
    if sid == 'D01':
        # plaid cloth inserts on both front seats (backrest and cushion)
        for sgn in (-1, 1):
            x = sgn * 0.37
            base = Vector((x, ys - 0.225, zc + 0.34))
            for k, dz in enumerate((-0.18, -0.09, 0.0, 0.09, 0.18)):
                off = Vector((0, 0.0117 + 0.218 * dz, -0.0026 + 0.976 * dz))
                A(box('plaid', base + off, (0.22, 0.004, 0.012), M['plaid_b'] if k % 2 else M['plaid_a'], rot=(-0.22, 0, 0)))
            for dx in (-0.06, 0.06):
                A(box('plaid', base + Vector((dx, 0.0117, -0.0026)), (0.01, 0.004, 0.48), M['plaid_a'], rot=(-0.22, 0, 0)))
            for dy in (-0.14, 0.0, 0.14):
                A(box('plaid', (x, ys + 0.02 + dy, zc + 0.057), (0.22, 0.012, 0.004), M['plaid_a']))
            for dx in (-0.06, 0.06):
                A(box('plaid', (x + dx, ys + 0.02, zc + 0.057), (0.01, 0.42, 0.004), M['plaid_b']))
        # red piping on the dash face, a cassette deck with a tape in it and a graphic equaliser
        A(box('dash_piping', (0.1, yr - 0.002, tr - 0.1), (2 * W - 0.5, 0.006, 0.008), M['accent']))
        td = Vector((0, yr + 0.064, (kz + z_ramp1) / 2 + 0.01))
        A(box('tape_deck', td, (0.2, 0.012, 0.055), M['dark'], 0.004))
        A(box('tape_slot', td + Vector((-0.03, -0.007, 0.005)), (0.1, 0.004, 0.018), M['line']))
        A(box('tape', td + Vector((-0.03, -0.014, 0.005)), (0.09, 0.012, 0.012), M['plaid_b'], 0.002))
        for k in range(7):
            A(box('eq_bar', td + Vector((0.045 + k * 0.009, -0.007, -0.01 + 0.004 * (k % 3))), (0.005, 0.004, 0.016 + 0.006 * (k % 3)), M['warn_green']))
        # a stack of tapes in the door pocket
        for k in range(3):
            A(box('cassette', (-W + 0.07, (yf - 0.05 + min(ys - 0.35, c.y_rw0 + 0.1)) / 2 + 0.1, floor + 0.2 + k * 0.018), (0.012, 0.1, 0.016), M['plaid_b'] if k % 2 else M['accent'], 0.002))
    elif sid == 'D02':
        # switch pods either side of the binnacle: rocker switches under the driver's fingertips
        for sgn in (-1, 1):
            pc = Vector((xd + sgn * (bw + 0.055), G.y + 0.03, G.z + 0.01))
            A(box('switch_pod', pc, (0.1, 0.09, 0.13), M['plastic'], 0.02, rot=(0.25, 0, sgn * -0.25), seg=2))
            for r_ in range(3):
                for k in range(2):
                    A(box('rocker', pc + Vector((-0.02 + 0.04 * k, -0.047, 0.035 - 0.035 * r_)), (0.026, 0.01, 0.018),
                          M['warn_amber'] if (r_ == 0 and k == 1) else M['dark'], 0.003, rot=(0.25, 0, sgn * -0.25)))
        # a CD player on the lower stack, leather knee pads on the console, an aluminium footrest
        td = Vector((0, yr + 0.064, (kz + z_ramp1) / 2 + 0.01))
        A(box('cd_player', td, (0.2, 0.012, 0.05), M['dark'], 0.004))
        A(box('cd_slot', td + Vector((0, -0.007, 0.012)), (0.12, 0.004, 0.004), M['line']))
        A(box('cd_screen', td + Vector((0.04, -0.007, -0.008)), (0.07, 0.004, 0.014), M['screen']))
        A(box('alu_footrest', (xd - 0.25, yf - 0.155, floor + 0.13), (0.075, 0.006, 0.15), M['alu'], 0.004, rot=(0.6, 0, 0)))
    elif sid == 'D03':
        # full roll cage: main hoop (above) + A-pillar bars, a windscreen bar, door X bars, rear stays
        for sgn in (-1, 1):
            # along the roof edge above the door (out of the driver's view) to the windscreen header
            xa = sgn * (W - 0.16)
            yfa = c.y_roof0 + 0.03
            za = c.top_z(xa, yfa) - 0.08
            ym_ = (hy + yfa) / 2
            A(tube_along('cage_roof', [Vector((sgn * (W - 0.13), hy, ztop - 0.1)), Vector((sgn * (W - 0.15), ym_, c.top_z(sgn * (W - 0.15), ym_) - 0.08)), Vector((xa, yfa, za))], 0.02, M['cage'], sides=10))
            A(tube_along('cage_a_pillar', [Vector((xa, yfa, za)), Vector((sgn * (W - 0.16), yf - 0.12, (za + floor) / 2 + 0.1)), Vector((sgn * (W - 0.12), yf - 0.1, floor + 0.02))], 0.02, M['cage'], sides=10))
            zlo, zhi = floor + 0.12, zb - 0.1
            A(tube_along('cage_door_x', [Vector((sgn * (W - 0.1), hy, zlo)), Vector((sgn * (W - 0.1), yf - 0.15, zhi))], 0.018, M['cage'], sides=8))
            A(tube_along('cage_door_x', [Vector((sgn * (W - 0.1), hy, zhi)), Vector((sgn * (W - 0.1), yf - 0.15, zlo))], 0.018, M['cage'], sides=8))
            for yy in (hy, yf - 0.1):
                A(box('cage_foot', (sgn * (W - 0.11), yy, floor + 0.008), (0.07, 0.07, 0.01), M['alu'], 0.003))
            # aluminium door skins instead of trimmed cards, a red door-pull strap
            A(box('alu_door', (sgn * (W - 0.035), (y0d + y1d) / 2, floor + (zb - floor) * 0.5), (0.006, (y0d - y1d) * 0.92, (zb - floor) * 0.7), M['alu']))
            A(box('door_strap', (sgn * (W - 0.045), ys + 0.25, zc + 0.3), (0.012, 0.16, 0.03), M['harness_red']))
            # shell-seat head wings and red 6-point harnesses on both seats
            x = sgn * 0.37
            for bx in (-1, 1):
                A(box('head_wing', (x + bx * 0.16, ys - 0.36, zh - 0.02), (0.05, 0.14, 0.2), M['seat'], 0.03, rot=(-0.22, 0, bx * 0.3)))
            for dx in (-0.07, 0.07):
                A(tube_along('seat_harness', [Vector((x + dx, hy, zh - 0.27)), Vector((x + dx * 0.9, ys - 0.25, zh - 0.3)), Vector((x + dx * 0.7, ys - 0.12, zc + 0.35)), Vector((x + dx * 0.5, ys + 0.02, zc + 0.1))], 0.022, M['harness_red'], sides=4))
            A(tube_along('lap_harness', [Vector((x - 0.2, ys - 0.1, zc + 0.05)), Vector((x, ys + 0.05, zc + 0.12)), Vector((x + 0.2, ys - 0.1, zc + 0.05))], 0.022, M['harness_red'], sides=4))
        yh_ = c.y_roof0 + 0.03  # the windscreen header
        A(tube_along('cage_windscreen', [Vector((-(W - 0.16), yh_, c.top_z(-(W - 0.16), yh_) - 0.08)), Vector((0, yh_, c.top_z(0, yh_) - 0.07)), Vector((W - 0.16, yh_, c.top_z(W - 0.16, yh_) - 0.08))], 0.018, M['cage'], sides=10))
        A(tube_along('cage_diag', [Vector((-(W - 0.3), hy, ztop)), Vector((W - 0.12, hy, floor + 0.05))], 0.018, M['cage'], sides=10))
        # trip computer and stopwatch on the co-driver's dash, a toggle-switch panel, extinguisher, helmet
        tc = Vector((0.42, yr + 0.1, dash_top(yr + 0.1) + 0.035))
        A(box('trip_computer', tc, (0.16, 0.06, 0.06), M['dark'], 0.008, rot=(0.35, 0, 0)))
        A(box('trip_screen', tc + Vector((0, -0.032, 0.008)), (0.11, 0.004, 0.028), M['warn_green'], rot=(0.35, 0, 0)))
        for k in range(3):
            A(box('trip_button', tc + Vector((-0.045 + 0.045 * k, -0.034, -0.02)), (0.02, 0.006, 0.01), M['warn_amber'] if k == 1 else M['plastic'], 0.002, rot=(0.35, 0, 0)))
        tp = Vector((0, yr + 0.064, (kz + z_ramp1) / 2 + 0.01))
        A(box('toggle_panel', tp, (0.21, 0.008, 0.07), M['alu'], 0.004))
        for k in range(5):
            q = tp + Vector((-0.08 + 0.04 * k, -0.006, 0.0))
            A(box('toggle_cover', q + Vector((0, -0.008, 0.008)), (0.022, 0.014, 0.03), M['tail'], 0.004, rot=(0.5, 0, 0)))
            A(cyl('toggle', q + Vector((0, -0.01, -0.012)), 0.003, 0.02, M['chrome'], rot=(math.pi / 2, 0, 0), verts=6))
        ex = Vector((0.37, ys + 0.5, floor + 0.075))
        A(cyl('extinguisher', ex, 0.045, 0.3, M['extinguisher'], rot=(math.pi / 2, 0, 0), verts=16))
        A(cyl('extinguisher_head', ex + Vector((0, 0.17, 0)), 0.025, 0.05, M['chrome'], rot=(math.pi / 2, 0, 0), verts=10))
        for dy in (-0.08, 0.08):
            A(box('extinguisher_strap', ex + Vector((0, dy, 0)), (0.1, 0.02, 0.1), M['alu'], 0.004))
        A(_sphere('helmet', (0.37, ys + 0.02, zc + 0.18), 0.13, M['helmet'], scale=(0.95, 1.1, 0.95), seg=20, rings=12))
        A(box('helmet_visor', (0.37, ys + 0.14, zc + 0.19), (0.15, 0.03, 0.07), M['lens_dark'], 0.02))
        A(box('helmet_stripe', (0.37, ys + 0.02, zc + 0.31), (0.03, 0.24, 0.01), M['accent'], 0.005))
        # hydraulic handbrake: a tall lever beside the gear stick
        hb2 = Vector((0.08, ys + 0.25, floor + 0.33))
        A(box('hydro_handbrake', hb2 + Vector((0, 0, 0.12)), (0.022, 0.03, 0.24), M['alu'], 0.006, rot=(-0.25, 0, 0)))
        A(cyl('hydro_grip', hb2 + Vector((0, -0.03, 0.25)), 0.02, 0.07, M['harness_red'], rot=(-0.25, 0, 0), verts=10))
        # strut brace across the stripped rear
        A(tube_along('strut_brace', [Vector((-(W - 0.2), -c.wb / 2, floor + 0.55)), Vector((W - 0.2, -c.wb / 2, floor + 0.55))], 0.02, M['cage'], sides=10))
        A(box('stopwatch_mount', sw_c + Vector((0, 0.01, -0.02)), (0.04, 0.03, 0.012), M['dark']))
    elif sid == 'D04':
        # split bench: a fold-down armrest and a continuous backrest between the seats, tuck-and-roll pleats
        A(box('bench_back', (0, ys - 0.3, zc + 0.34), (0.34, 0.09, 0.6), M['seat_fabric'], 0.03, rot=(-0.22, 0, 0)))
        A(box('bench_armrest', (0, ys - 0.06, zc + 0.2), (0.15, 0.42, 0.11), M['seat'], 0.04, seg=2))
        for sgn in (-1, 0, 1):
            xs_ = sgn * 0.37
            wdt = 0.3 if sgn else 0.3
            for k in range(6):
                dx = -wdt / 2 + wdt * (k + 0.5) / 6
                A(box('pleat', (xs_ + dx, ys - 0.26 + 0.004, zc + 0.36), (0.006, 0.012, 0.5), M['stitch'], rot=(-0.22, 0, 0)))
        # wood trim across the passenger dash and on the stack, chrome door-handle rings
        A(box('wood_trim', (0.37, yr - 0.004, tr - 0.07), (0.44, 0.012, 0.06), M['wood'], 0.006))
        for k in range(4):
            A(box('wood_grain', (0.2 + k * 0.11, yr - 0.011, tr - 0.07 + 0.012 * ((k % 2) * 2 - 1)), (0.08, 0.002, 0.004), M['wood_grain']))
        A(box('stack_wood', (0, cy - 0.024, tr - 0.2), (0.3, 0.006, 0.02), M['wood'], 0.003))
        # CB radio hanging under the passenger dash, the handset on a hook with a coiled cord
        cb = Vector((0.3, yr + 0.12, kz - 0.06))
        A(box('cb_radio', cb, (0.17, 0.13, 0.05), M['dark'], 0.006))
        A(box('cb_face', cb + Vector((0, -0.066, 0)), (0.16, 0.004, 0.04), M['grey']))
        A(box('cb_display', cb + Vector((-0.04, -0.069, 0.005)), (0.04, 0.004, 0.016), M['warn_red']))
        for k in range(3):
            A(cyl('cb_knob', cb + Vector((0.01 + 0.03 * k, -0.072, 0)), 0.009, 0.012, M['chrome'], rot=(math.pi / 2, 0, 0), verts=10))
        mic = cb + Vector((0.11, -0.05, -0.02))
        A(box('cb_mic', mic, (0.04, 0.03, 0.07), M['dark'], 0.012))
        coil = [mic + Vector((0.012 * math.cos(t * 1.2), 0.012 * math.sin(t * 1.2), -0.035 - 0.012 * t)) for t in range(16)]
        coil += [coil[-1] + Vector((-0.06, 0.03, 0.1))]
        A(tube_along('cb_cord', coil, 0.003, M['dark'], sides=4))
        # a thermos and work gloves on the seat, a cap on the dash
        A(cyl('thermos', (0.3, ys + 0.08, zc + 0.16), 0.04, 0.24, M['thermos'], rot=(0, math.pi / 2 - 0.2, 0), verts=14))
        A(cyl('thermos_cup', (0.43, ys + 0.1, zc + 0.14), 0.042, 0.06, M['grey'], rot=(0, math.pi / 2 - 0.2, 0), verts=14))
        cap = Vector((0.45, yf - 0.2, dash_top(yf - 0.2) + 0.03))
        A(_sphere('cap', cap, 0.075, M['accent'], scale=(1.0, 1.1, 0.55), seg=16, rings=8))
        A(box('cap_peak', cap + Vector((0, -0.08, -0.02)), (0.1, 0.07, 0.008), M['accent'], 0.02, rot=(-0.15, 0, 0)))
    # drop the generic details that do not belong to this car
    drop = set(ST.get('drop', ()))
    keep = []
    for o in items:
        if o.name.split('.')[0] in drop:
            H['bpy'].data.objects.remove(o)
        else:
            keep.append(o)
    items[:] = keep
    c.add(*items)


# ------------------------------------------------------------------------------------------------
# driver character

def driver(c, seed=0):
    """Comic driver (Telltale / Borderlands flavour): strong jaw, heavy brows, stubble, messy hair,
    shades pushed up, ink hatching on the face; bomber jacket over a tee, jeans, sneakers, fingerless
    driving gloves with real fingers wrapped round the rim."""
    M = c.M
    box, cyl, torus, tube_along = H['box'], H['cyl'], H['torus'], H['tube_along']
    rnd = random.Random(seed)
    ys, zc, zh = c.seat
    SW = c.wheel_centre
    x = -0.37
    items = []
    A = items.append
    hip = Vector((x, ys - 0.02, zc + 0.12))
    chest = Vector((x, ys - 0.17, zh - 0.36))
    neck_base = Vector((x, ys - 0.2, zh - 0.165))
    Hc = Vector((x, ys - 0.21, zh))
    up = (chest - hip).normalized()
    lean = -math.atan2(up.y, up.z)
    front = Vector((0, math.cos(lean), -math.sin(lean)))

    anchor = H['bpy'].data.objects.new('DRIVER_ANCHOR', None)  # the hip: where a car seats the driver
    H['bpy'].context.scene.collection.objects.link(anchor)
    anchor.location = hip
    c.markers = getattr(c, 'markers', []) + [anchor]

    # --- lower body: hips (fixed in the seat), thigh / shin bones and feet the game poses onto the
    # pedals with two-bone IK (D-076); authored here in a resting pose
    c.animated('DRIVER_HIPS', [
        box('pelvis', hip + Vector((0, 0.03, -0.02)), (0.34, 0.26, 0.14), M['jeans'], 0.06, seg=3),
        box('belt', hip + Vector((0, 0.03, 0.06)), (0.35, 0.27, 0.035), M['shoe_dark'], 0.01),
        box('belt_buckle', hip + Vector((0, 0.17, 0.06)), (0.05, 0.01, 0.035), M['chrome'], 0.005)], hip)
    for sgn, tag in ((-1, 'L'), (1, 'R')):
        hp = hip + Vector((sgn * 0.1, 0.06, -0.04))
        ankle = hip + Vector((sgn * 0.13, 0.80, -0.14))
        to = ankle - hp
        d = min(to.length, LEG_UPPER + LEG_LOWER - 0.01)
        dirv = to.normalized()
        pole = Vector((sgn * 0.15, 0.3, 1.0))
        nrm = (pole - dirv * pole.dot(dirv)).normalized()
        ca = (LEG_UPPER ** 2 + d * d - LEG_LOWER ** 2) / (2 * LEG_UPPER * d)
        knee = hp + dirv * (LEG_UPPER * ca) + nrm * (LEG_UPPER * math.sqrt(max(0.0, 1 - ca * ca)))
        ankle = hp + dirv * d
        thigh = [_limb('thigh', LEG_UPPER, [(0.0, 0.075), (0.4, 0.071), (0.85, 0.06), (1.0, 0.056)], M['jeans'], flat=0.9),
                 box('jeans_seam', Vector((sgn * 0.066, 0, LEG_UPPER * 0.5)), (0.004, 0.004, LEG_UPPER * 0.8), M['ink'])]
        shin = [_sphere('knee', (0, 0, 0), 0.062, M['jeans'], seg=12, rings=8),
                _limb('shin', LEG_LOWER, [(0.0, 0.056), (0.35, 0.053), (0.8, 0.043), (1.0, 0.047)], M['jeans'], flat=0.9),
                H['cyl']('jeans_hem', Vector((0, 0, LEG_LOWER - 0.01)), 0.05, 0.03, M['jeans'], rot=(0, 0, 0), verts=14)]
        c.bone('THIGH_' + tag, thigh, hp, knee - hp)
        c.bone('SHIN_' + tag, shin, knee, ankle - knee)
        rx = -0.5  # sole along the pedal face
        def on_foot(v):
            return ankle + Vector((v[0], v[1] * math.cos(rx) - v[2] * math.sin(rx), v[1] * math.sin(rx) + v[2] * math.cos(rx)))
        c.animated('FOOT_' + tag, [
            box('sneaker', on_foot((0, 0.07, -0.05)), (0.1, 0.24, 0.08), M['jacket_dark'], 0.03, seg=2, rot=(rx, 0, 0)),
            box('sole', on_foot((0, 0.07, -0.095)), (0.105, 0.25, 0.025), M['shoe'], 0.01, rot=(rx, 0, 0)),
            box('laces', on_foot((0, 0.1, -0.008)), (0.04, 0.1, 0.006), M['shoe'], rot=(rx, 0, 0)),
            box('sneaker_stripe', on_foot((sgn * 0.051, 0.06, -0.05)), (0.004, 0.14, 0.03), M['accent'], rot=(rx, 0, 0))], ankle)

    # --- torso (sways in the game): a lofted body in an open bomber jacket over a tee
    torso = []
    T = torso.append
    axis = (neck_base - hip)
    L = axis.length
    u = axis.normalized()
    side = Vector((1, 0, 0))
    fw = u.cross(side).normalized() * -1  # towards the car front, square to the spine
    # (height 0..1 along the spine, half width, half depth, forward offset)
    secs = [(0.0, 0.17, 0.13, 0.0), (0.18, 0.165, 0.12, 0.01), (0.38, 0.16, 0.115, 0.012), (0.58, 0.19, 0.125, 0.02),
            (0.78, 0.215, 0.12, 0.02), (0.9, 0.212, 0.112, 0.01), (0.96, 0.17, 0.1, 0.0), (1.0, 0.095, 0.075, 0.0), (1.03, 0.07, 0.06, 0.0)]
    n = 20
    rings = []
    for h, hw, hd, fo in secs:
        c0 = hip + u * (h * L) + fw * fo
        ring = []
        for k in range(n):
            a = 2 * math.pi * k / n
            # superellipse: squarer shoulders and chest than a tube
            cx, sy = math.cos(a), math.sin(a)
            ex = math.copysign(abs(cx) ** 0.75, cx)
            ey = math.copysign(abs(sy) ** 0.85, sy)
            ring.append(tuple(c0 + side * (ex * hw) + fw * (ey * hd)))
        rings.append(ring)
    verts = [v for r in rings for v in r]
    faces = []
    for ri in range(len(rings) - 1):
        for k in range(n):
            a0, a1 = ri * n + k, ri * n + (k + 1) % n
            faces.append((a0, a1, a1 + n, a0 + n))
    faces.append(tuple(range(n))[::-1])
    jk = H['link']('jacket_body', verts, faces, [M['jacket']])
    H['fix_normals'](jk)
    for p_ in jk.data.polygons:
        p_.use_smooth = True
    T(jk)
    # the open front: tee showing between the jacket edges, zip rails, collar, pockets, patch
    chest_c = hip + u * (0.62 * L) + fw * 0.02
    front_pt = lambda h, dx: hip + u * (h * L) + fw * ({0.2: 0.13, 0.4: 0.128, 0.62: 0.147, 0.8: 0.142}.get(h, 0.13)) + side * dx
    for h in (0.2, 0.4, 0.62, 0.8):
        T(box('tee_panel', front_pt(h, 0) + fw * 0.002, (0.1, 0.006, L * 0.21), M['shirt'], 0.004, rot=(lean, 0, 0)))
    T(box('tee_print', front_pt(0.62, 0) + fw * 0.006, (0.06, 0.004, 0.06), M['accent'], 0.004, rot=(lean, 0, 0)))
    for sgn in (-1, 1):
        T(tube_along('zip_rail', [front_pt(0.1, sgn * 0.052), front_pt(0.4, sgn * 0.055), front_pt(0.8, sgn * 0.058)], 0.006, M['jacket_dark'], sides=6))
        T(box('pocket_flap', front_pt(0.3, sgn * 0.12) + fw * 0.004, (0.1, 0.012, 0.028), M['jacket_dark'], 0.004, rot=(lean, 0, sgn * 0.08)))
        T(tube_along('side_seam', [hip + side * (sgn * 0.17) + u * 0.02, hip + side * (sgn * 0.2) + u * (0.75 * L)], 0.003, M['ink'], sides=4))
    T(box('jacket_patch', front_pt(0.62, -0.13) + fw * 0.004, (0.06, 0.004, 0.045), M['accent'], rot=(lean, 0, 0)))
    T(box('waist_band', hip + u * 0.04, (0.35, 0.27, 0.05), M['jacket_dark'], 0.02, seg=2, rot=(lean, 0, 0)))
    for sgn in (-1, 1):  # four-point harness over the jacket (the seat has the slots)
        T(tube_along('harness', [hip + u * (0.95 * L) + side * (sgn * 0.09) - fw * 0.05, front_pt(0.8, sgn * 0.085) + fw * 0.01, front_pt(0.2, sgn * 0.075) + fw * 0.02], 0.02, M['harness'], sides=4))
        T(box('harness_adjuster', front_pt(0.5, sgn * 0.08) + fw * 0.025, (0.055, 0.008, 0.02), M['chrome'], rot=(lean, 0, 0)))
    T(box('lap_belt', hip + Vector((0, 0.16, 0.03)), (0.38, 0.012, 0.05), M['harness']))
    T(cyl('harness_buckle', hip + Vector((0, 0.17, 0.06)), 0.032, 0.014, M['chrome'], rot=(math.pi / 2, 0, 0), verts=16))
    collar = _limb('collar_rib', 0.06, [(0.0, 0.1), (0.3, 0.088), (1.0, 0.072)], M['jacket_dark'], sides=24, flat=0.85)
    collar.location = neck_base - u * 0.02
    collar.rotation_mode = 'QUATERNION'
    collar.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(u)
    T(collar)
    # the neck: thick at the base, narrowing under the jaw (D-070: the straight 0.058 tube read as a pillar)
    nk = _limb('neck', 0.17, [(0.0, 0.064), (0.3, 0.055), (0.7, 0.05), (1.0, 0.05)], M['skin'], sides=20, flat=0.9)
    nk.location = neck_base + Vector((0, -0.01, -0.05))
    nk.rotation_mode = 'QUATERNION'
    nk.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(Vector((0, 0.1, 1.0)).normalized())
    for p_ in nk.data.polygons:
        p_.use_smooth = True
    T(nk)
    T(_sphere('adams_apple', neck_base + Vector((0, 0.05, 0.03)), 0.013, M['skin'], seg=8, rings=6))
    c.animated('DRIVER_TORSO', torso, hip)

    # --- heads v6 (D-065): three drivers in the Telltale "The Wolf Among Us" manner - real proportions,
    # heavy ink shadow shapes (brow, sockets, under the nose and lip, jaw), stubble drawn as ink dashes.
    # DRIVER_HEAD (v0), DRIVER_HEAD_1, DRIVER_HEAD_2 share a pivot; the game shows one per car.
    #   v0 "wolf": long messy hair over the ears, heavy stubble, tired eyes
    #   v1 "veteran": grey crop, full grey beard, darker skin
    #   v2 "punk": undercut and swept quiff, goatee, shades pushed up
    rx, ry, rz = 0.074, 0.096, 0.122  # D-078: longer, narrower - a grown man, not a toy
    mat = H['mat']
    looks = [
        dict(skin=(0.92, 0.62, 0.44), hair=(0.24, 0.13, 0.06), beard=(0.2, 0.11, 0.06), iris=(0.55, 0.42, 0.12)),
        dict(skin=(0.6, 0.37, 0.24), hair=(0.62, 0.6, 0.57), beard=(0.66, 0.64, 0.6), iris=(0.25, 0.16, 0.08)),
        dict(skin=(0.95, 0.72, 0.56), hair=(0.07, 0.06, 0.06), beard=(0.09, 0.07, 0.06), iris=(0.2, 0.42, 0.55)),
    ]

    def on_head(d, lift=0.0):
        d = Vector(d).normalized()
        t = 1.0 / math.sqrt((d.x / rx) ** 2 + (d.y / ry) ** 2 + (d.z / rz) ** 2)
        return Hc + d * (t + lift)

    def g(x, y, z, cx, cz, sx, sz):
        return math.exp(-(((x - cx) / sx) ** 2 + ((z - cz) / sz) ** 2)) if y > 0 else 0.0

    def relief(d):
        """Face planes (the nose is its own mesh): brow ridge, deep sockets, high cheekbones, muzzle,
        a real chin; the temples and under-cheeks hollow."""
        x, y, z = d.x, d.y, d.z
        r = 0.0
        r += 0.017 * g(x, y, z, 0.0, 0.3, 0.6, 0.075)                                  # heavy brow ridge
        r -= 0.018 * (g(x, y, z, 0.36, 0.16, 0.15, 0.085) + g(x, y, z, -0.36, 0.16, 0.15, 0.085))  # deep sockets
        r += 0.013 * (g(x, y, z, 0.55, 0.0, 0.15, 0.1) + g(x, y, z, -0.55, 0.0, 0.15, 0.1))          # high cheekbones
        r += 0.009 * g(x, y, z, 0.0, -0.36, 0.32, 0.16)
        r += 0.016 * g(x, y, z, 0.0, -0.8, 0.26, 0.12)
        r -= 0.007 * (g(x, y, z, 0.47, -0.3, 0.12, 0.14) + g(x, y, z, -0.47, -0.3, 0.12, 0.14))
        r -= 0.004 * (g(x, y, z, 0.78, 0.35, 0.12, 0.15) + g(x, y, z, -0.78, 0.35, 0.12, 0.15))
        return r

    def sculpt(o, jaw_w=0.1):
        for v in o.data.vertices:
            v.co = surf(v.co.normalized(), jaw_w)

    def surf(d, jaw_w=0.1):
        """The sculpted head surface point (relative to Hc) in the unit direction d."""
        if True:
            p = Vector((d.x * rx, d.y * ry, d.z * rz))
            if p.z < 0:  # the jaw: square corners stay wide, then a broad chin that comes forward
                k = min(1.0, -p.z / rz)
                p.x *= 1.0 + 0.07 * math.exp(-((k - 0.62) / 0.16) ** 2) - jaw_w * max(0.0, k - 0.72) ** 2 * 3.0
                if p.y > 0:
                    p.y += 0.017 * k ** 1.5
                if abs(d.x) > 0.4:
                    p.z *= 1.0 - 0.1 * k ** 2
            if p.y < 0:
                p.y *= 0.99
            p += d * relief(d)
            return p

    def shell(name, keep, m, grow, jaw_w):
        """A layer over the sculpted head: the faces whose (sculpted) centre keep() accepts, the cut edge
        rounded off on the unit sphere, then sculpted and pushed out by grow (metres or a function)."""
        o = _sphere(name, (0, 0, 0), 0.1, m, seg=112, rings=56)  # fine, so cut edges read as curves
        bm = bmesh.new()
        bm.from_mesh(o.data)
        for v in bm.verts:
            v.co = v.co.normalized()
        for f in list(bm.faces):
            cc = sum((surf(v.co, jaw_w) for v in f.verts), Vector()) / len(f.verts)
            if not keep(cc):
                bm.faces.remove(f)
        for v in list(bm.verts):
            if not v.link_faces:
                bm.verts.remove(v)
        # Taubin-smooth the boundary loop (alternating shrink / inflate keeps holes such as the face
        # opening in the hair their size) in direction space, so nothing drifts
        for it in range(16):
            lam = 0.5 if it % 2 == 0 else -0.53
            moved = {}
            for v in bm.verts:
                if v.is_boundary:
                    nb = [e.other_vert(v) for e in v.link_edges if e.is_boundary]
                    if len(nb) == 2:
                        moved[v] = (v.co + ((nb[0].co + nb[1].co) * 0.5 - v.co) * lam).normalized()
            for v, q in moved.items():
                v.co = q
        for v in bm.verts:
            p = surf(v.co.normalized(), jaw_w)
            v.co = p + p.normalized() * (grow(p) if callable(grow) else grow)
        bm.to_mesh(o.data)
        bm.free()
        for p_ in o.data.polygons:
            p_.use_smooth = True
        o.location = Hc
        return o

    def blade(name, base, sweep, ln, width, m, flat=0.45):
        lock = _cone(name, base + sweep * ln * 0.45, width, 0.002, ln, m, verts=4)
        lock.scale = (1.0, flat, 1.0)
        lock.rotation_mode = 'QUATERNION'
        lock.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(sweep)
        return lock

    def lock(name, root, d0, length, width, m, gravity=0.35, segs=7, hug=0.008):
        """A hair lock as a flat, tapering ribbon (diamond section) that follows the head and falls
        under gravity - long hair that hangs, not spikes (D-068)."""
        pts, dirs = [Vector(root)], []
        d = Vector(d0).normalized()
        step = length / segs
        for k in range(segs):
            d = (d + Vector((0, 0, -gravity))).normalized()
            q = pts[-1] + d * step
            rel = q - Hc
            dist_min = (on_head(rel, 0.0) - Hc).length + hug
            if rel.length < dist_min:  # stay on the outside of the skull
                q = Hc + rel.normalized() * dist_min
                d = (q - pts[-1]).normalized()
            pts.append(q)
            dirs.append(d)
        dirs.append(dirs[-1])
        verts, faces = [], []
        for k, (p, d) in enumerate(zip(pts, dirs)):
            out = (p - Hc).normalized()
            side = d.cross(out).normalized()
            n_ = side.cross(d).normalized()
            t_ = k / (len(pts) - 1)
            w = width * math.sqrt(max(0.0, 1.0 - t_ ** 3)) + 0.003  # full along the lock, rounded at the tip
            verts += [tuple(p + side * w), tuple(p + n_ * w * 0.22), tuple(p - side * w), tuple(p - n_ * w * 0.22)]
        for k in range(len(pts) - 1):
            for j in range(4):
                a, b = k * 4 + j, k * 4 + (j + 1) % 4
                faces.append((a, b, b + 4, a + 4))
        faces.append((3, 2, 1, 0))
        o = H['link'](name, verts, faces, [m])
        H['fix_normals'](o)
        for p_ in o.data.polygons:
            p_.use_smooth = True
        return o

    def build_head(v):
        L = looks[v]
        sfx = '_%d' % v
        HM = dict(skin=mat('toon_skin' + sfx, L['skin'], 0.8),
                  hair=mat('toon_hair' + sfx, L['hair'], 0.9),
                  beard=mat('toon_beard' + sfx, L['beard'], 0.95),
                  shadow=mat('toon_face_shadow' + sfx + '_NoInk', tuple(c * 0.42 for c in L['skin']), 1.0),
                  stubble=mat('toon_stubble' + sfx + '_NoInk', tuple(c * 0.8 for c in L['skin']), 0.95),
                  shade=mat('toon_face_shade' + sfx + '_NoInk', tuple(c * 0.74 for c in L['skin']), 1.0),
                  iris=mat('toon_iris' + sfx + '_NoInk', L['iris'], 0.3),
                  ear_in=mat('toon_ear_in' + sfx, tuple(c * 0.82 for c in L['skin']), 0.85),
                  lips=mat('toon_lips' + sfx + '_NoInk', (L['skin'][0] * 0.72, L['skin'][1] * 0.5, L['skin'][2] * 0.5), 0.7))
        rnd_h = random.Random(seed * 7 + v)
        jaw_w = (0.1, 0.06, 0.14)[v]
        head = []
        Hd = head.append

        def on_face(d, lift=0.0):
            # a point on the sculpted skin (not the plain ellipsoid), lifted along the surface normal:
            # ink lines lie on the face instead of floating off it (D-078)
            d = Vector(d).normalized()
            p0 = surf(d, jaw_w)
            e1 = d.orthogonal().normalized()
            e2 = d.cross(e1).normalized()
            a_ = surf((d + e1 * 0.01).normalized(), jaw_w) - p0
            b_ = surf((d + e2 * 0.01).normalized(), jaw_w) - p0
            n_ = a_.cross(b_).normalized()
            if n_.dot(d) < 0:
                n_ = -n_
            return Hc + p0 + n_ * lift
        skull = _sphere('head', (0, 0, 0), 0.1, HM['skin'], seg=112, rings=56)
        sculpt(skull, jaw_w)

        # Face tones as a vertex-colour multiplier on the one skin material (D-068). Separate
        # materials or offset shells both drew an ink line round every tone in the game (the outline
        # pass inks material edges and depth steps); vertex colour has no edge and blends softly.
        # Shadow shapes as designed, tinted tones (research R3): skin shadows go warm red-brown, not grey
        # (Guilty Gear Xrd's shadow colours; Valve's TF2 paper). Grey tones read as bruises. Per-vertex
        # colours (one polygon of blend, ~5 mm) keep a clean edge; per-face colours stair-stepped.
        tone_of = {id(HM['shadow']): (0.6, 0.38, 0.36), id(HM['stubble']): (0.62, 0.52, 0.5),
                   id(HM['shade']): (0.8, 0.66, 0.62), id(HM['lips']): (0.8, 0.52, 0.5)}
        pcol = {}

        def paint(keep, m):
            """Tint the skull faces whose centre keep() accepts (later paints win)."""
            t = tone_of[id(m)]
            for poly in skull.data.polygons:
                if keep(Vector(poly.center)):
                    for vi in poly.vertices:
                        pcol[vi] = t
        for p_ in skull.data.polygons:
            p_.use_smooth = True
        skull.location = Hc
        Hd(skull)
        # ink shadow shapes (Telltale comic look): under the brow into the sockets, under the cheekbones,
        # under the nose, under the lower lip, and the jaw's underside
        paint(lambda cc: cc.y > 0.055 and 0.013 < abs(cc.x) < 0.057 and cc.z < 0.029 and cc.z > 0.012 + 6.0 * (abs(cc.x) - 0.035) ** 2, HM['shadow'])
        paint(lambda cc: cc.z < -0.085 and cc.y > -0.02 and not (abs(cc.x) < 0.03 and cc.y > 0.06), HM['shadow'])
        # wedge under each cheekbone, the side of the nose, the temples (D-078)
        paint(lambda cc: cc.y > 0.03 and 0.034 < abs(cc.x) < 0.066 and -0.06 < cc.z < -0.012 - (abs(cc.x) - 0.034) * 0.5 and cc.z > -0.06 + (abs(cc.x) - 0.034) * 0.6, HM['shade'])
        paint(lambda cc: cc.y > 0.05 and 0.012 < abs(cc.x) < 0.024 and -0.026 < cc.z < 0.012, HM['shade'])
        paint(lambda cc: cc.y > -0.01 and abs(cc.x) > 0.06 and 0.02 < cc.z < 0.06, HM['shade'])
        # stubble tone over the jaw and upper lip for v0 (and dashes), a full beard for v1, a goatee for v2
        muzzle = lambda cc: cc.y > 0.0 and cc.z < -0.028 and (cc.z < -0.045 or abs(cc.x) < 0.028)
        if v == 0:
            paint(lambda cc: (muzzle(cc) or (cc.y > -0.02 and cc.z < -0.02 and abs(cc.x) > 0.05)) and not (abs(cc.x) < 0.016 and -0.06 < cc.z < -0.035 and cc.y > 0.07), HM['stubble'])
        elif v == 1:
            Hd(shell('beard', lambda cc: cc.y > -0.03 and cc.z < -0.036 + max(0.0, abs(cc.x) - 0.03) * 0.7 and not (abs(cc.x) < 0.02 and -0.06 < cc.z < -0.04 and cc.y > 0.07),
                     HM['beard'], lambda co: 0.004 + 0.012 * max(0.0, min(1.0, (-co.z - 0.03) / 0.08)), jaw_w))
        else:
            Hd(shell('goatee', lambda cc: cc.y > 0.045 and cc.z < -0.068 and abs(cc.x) < 0.026 - max(0.0, -0.068 - cc.z) * 0.1, HM['beard'], 0.003, jaw_w))
        mo = on_face((0, 1.0, -0.47), 0.003)
        if v in (1, 2):
            Hd(tube_along('moustache', [on_head((-0.3, 1.0, -0.5), 0.01), on_head((-0.14, 1.0, -0.38), 0.016), mo + Vector((0, 0.004, 0.012)),
                                         on_head((0.14, 1.0, -0.38), 0.016), on_head((0.3, 1.0, -0.5), 0.01)], 0.0048 if v == 2 else 0.0068, HM['beard'], sides=8))
        # hair
        if v == 0:
            # long, messy, parted: a thick mass to the collar, strands falling over the forehead and ears
            Hd(shell('hair_mass', lambda cc: (cc.z > 0.045 or (cc.y < 0.02 and cc.z > -0.075) or (abs(cc.x) > 0.05 and cc.y < 0.045 and cc.z > -0.02))
                     and not (cc.y > 0.055 and cc.z < 0.066), HM['hair'], lambda co: 0.016 + 0.016 * max(0.0, min(1.0, -co.z / 0.08)), jaw_w))
            part = 0.28  # parting line, right of centre
            for k in range(18):   # front: swept back off the forehead, a few heavy strands falling forward
                sgn = -1 if k < 11 else 1
                a = part + sgn * rnd_h.uniform(0.05, 1.0)
                root = on_head((math.sin(a) * 0.45, 0.6 + 0.1 * rnd_h.random(), 0.8), 0.016)
                fall = k in (3, 13)  # two strands fall forward, clear of the eyes
                d0 = Vector((sgn * 1.0, 0.35, -0.3)) if fall else Vector((sgn * 0.45, -0.7, 0.45))
                Hd(lock('hair_lock', root, d0, rnd_h.uniform(0.07, 0.11) if fall else rnd_h.uniform(0.1, 0.15), rnd_h.uniform(0.022, 0.03), HM['hair'], gravity=0.35 if fall else 0.6, hug=0.02))
            for k in range(40):   # sides and back: heavy locks lying on the head, down to the nape
                a = rnd_h.uniform(1.65, 4.63)  # sides behind the ears and the back: never over the cheeks
                back = math.cos(a) < -0.3
                root = on_head((math.sin(a) * 0.7, math.cos(a) * 0.7, rnd_h.uniform(0.1, 0.55) if back else rnd_h.uniform(0.4, 0.8)), 0.018)
                out = (root - Hc).normalized()
                d0 = Vector((out.x * 0.15, out.y * 0.15, -1.0))
                Hd(lock('hair_lock', root, d0, rnd_h.uniform(0.09, 0.14), rnd_h.uniform(0.026, 0.034), HM['hair'], gravity=0.9, hug=0.026))
            for k in range(8):     # ink strand lines through the mass (sides and back only)
                a = math.copysign(rnd_h.uniform(1.3, 2.7), rnd_h.uniform(-1, 1))
                p0 = on_head((math.sin(a) * 0.4, math.cos(a) * 0.4, 0.92), 0.02)
                p1 = on_head((math.sin(a) * 0.95, math.cos(a) * 0.95, 0.1), 0.021)
                Hd(tube_along('hair_ink', [p0, (p0 + p1) / 2 + (p0 - Hc).normalized() * 0.006, p1], 0.0012, M['ink'], sides=3))
        elif v == 1:
            Hd(shell('hair_crop', lambda cc: (cc.z > 0.02 or (cc.y < 0.0 and cc.z > -0.05)) and not (cc.y > 0.05 and cc.z < 0.085) and not (abs(cc.x) > 0.06 and cc.y > -0.02 and cc.z < 0.045), HM['hair'], 0.004, jaw_w))
        else:
            Hd(shell('hair_side', lambda cc: cc.z > -0.01 - (0.03 if cc.y < 0 else 0.0) and not (cc.y > 0.02 and cc.z < 0.075) and not (abs(cc.x) > 0.05 and cc.y > -0.01 and cc.z < 0.03), HM['beard'], 0.003, jaw_w))
            Hd(shell('hair_top', lambda cc: cc.z > 0.058 - (0.02 if cc.y < 0 else 0.0) and not (cc.y > 0.06 and cc.z < 0.08), HM['hair'], 0.012, jaw_w))
            for k in range(18):
                a = -1.3 + 2.6 * k / 17 + rnd_h.uniform(-0.06, 0.06)
                el = rnd_h.uniform(0.95, 1.3)
                d = Vector((math.sin(a) * math.cos(el) * 0.9, math.cos(a) * math.cos(el), math.sin(el)))
                sweep = Vector((d.x * 0.6, -0.35, 0.9)).normalized() if d.y > 0.25 else Vector((d.x * 1.2, -0.9, 0.45)).normalized()
                Hd(blade('hair_lock', on_head(d, 0.006), sweep, rnd_h.uniform(0.035, 0.052) * (1.15 if d.y > 0.25 else 0.85), 0.026, HM['hair']))
            sh = on_head((0, 0.75, 0.78), 0.02)
            for sgn in (-1, 1):
                Hd(_sphere('shade_lens', sh + Vector((sgn * 0.034, 0.004, 0)), 0.022, M['lens_dark'], scale=(1.2, 0.35, 0.85), seg=12, rings=8))
                Hd(tube_along('shade_arm', [sh + Vector((sgn * 0.058, 0, 0)), on_head((sgn, 0.1, 0.45), 0.016)], 0.002, M['trim'], sides=4))
            Hd(box('shade_bridge', sh + Vector((0, 0.006, 0.006)), (0.02, 0.004, 0.004), M['trim']))
        # nose: a lofted bridge to a rounded tip, wings, nostrils, the ink shadow under it
        X, N = Vector((1, 0, 0)), Vector((0, 1, 0.12)).normalized()
        nsz = (1.0, 1.1, 0.92)[v]
        secs = [(0.22, 0.0, 0.008), (0.15, 0.007, 0.0085), (0.08, 0.012, 0.009), (0.0, 0.017, 0.0095), (-0.07, 0.021, 0.0105),
                (-0.12, 0.022, 0.0115), (-0.155, 0.017, 0.0115), (-0.18, 0.007, 0.009), (-0.19, 0.0, 0.006)]
        rings = []
        for zz, prot, wdt in secs:
            bp = Hc + surf(Vector((0, 1.0, zz)).normalized(), jaw_w) - N * 0.004
            rings.append([tuple(bp + X * (wdt * 1.1 * nsz * math.sin(a)) + N * ((prot * 1.35 * nsz + 0.006) * math.cos(a))) for a in [math.pi * (k / 13.0) - math.pi / 2 for k in range(14)]])
        nv = [q for r_ in rings for q in r_]
        nf = [(ri * 14 + k, ri * 14 + k + 1, ri * 14 + k + 15, ri * 14 + k + 14) for ri in range(len(rings) - 1) for k in range(13)]
        nose = H['link']('nose', nv, nf, [HM['skin']])
        H['fix_normals'](nose)
        for p_ in nose.data.polygons:
            p_.use_smooth = True
        Hd(nose)
        tip = on_head((0, 1.0, -0.13), 0.022 * nsz)
        for sgn in (-1, 1):
            Hd(_sphere('nose_wing', on_head((sgn * 0.13, 1.0, -0.2), -0.001), 0.0085, HM['skin'], scale=(0.8, 0.7, 0.9), seg=12, rings=8))
            Hd(tube_along('nostril', [on_head((sgn * 0.05, 1.0, -0.215), 0.012 * nsz), on_head((sgn * 0.1, 1.0, -0.215), 0.006)], 0.0016, M['ink'], sides=4))
        paint(lambda cc: cc.y > 0.07 and abs(cc.x) < 0.014 and -0.035 < cc.z < -0.026, HM['shade'])
        # ears
        for sgn in (-1, 1):
            ep = on_head((sgn, -0.08, 0.05), 0.002)
            Hd(_sphere('ear', ep + Vector((sgn * 0.004, -0.002, 0)), 0.022, HM['skin'], scale=(0.3, 0.72, 1.12), seg=16, rings=10))
            er = torus('ear_rim', ep + Vector((sgn * 0.008, -0.002, 0.002)), 0.018, 0.0045, HM['skin'], rot=(0, math.pi / 2, 0), seg=18, mseg=6)
            er.scale = (1.2, 0.7, 1.0)
            Hd(er)
            Hd(_sphere('ear_bowl', ep + Vector((sgn * 0.009, 0.001, -0.002)), 0.012, HM['ear_in'], scale=(0.3, 0.62, 0.95), seg=12, rings=8))
            Hd(_sphere('ear_lobe', ep + Vector((sgn * 0.006, 0.004, -0.024)), 0.009, HM['skin'], scale=(0.6, 0.9, 1.1), seg=10, rings=6))
        # eyes: almond white, iris, pupil, glint, heavy lash line, upper lid (lower on the tired v0),
        # lower lid, bags; brows (heavier and lower on v0/v1)
        lid = (0.003, 0.0045, 0.0055)[v]  # hooded: the upper lid sits low over the iris (TWAU, D-078)
        for sgn in (-1, 1):
            # -0.011 buried the eye white 1-4 mm inside the skull: the eyes read as closed squints
            # (research R1, docs/research/character-design.md); now it sits 4 mm proud of the skin
            e = on_face((sgn * 0.36, 1.0, 0.16), -0.003)
            Hd(_sphere('eye_white', e, 0.0145, M['eye_white'], scale=(1.18, 0.5, 0.56), seg=16, rings=8))
            Hd(_sphere('iris', e + Vector((-sgn * 0.001, 0.0062, -0.0012)), 0.0074, HM['iris'], scale=(1, 0.45, 1), seg=12, rings=6))
            Hd(_sphere('pupil', e + Vector((-sgn * 0.001, 0.0082, -0.0012)), 0.0031, M['ink'], scale=(1, 0.45, 1), seg=8, rings=5))
            Hd(_sphere('eye_glint', e + Vector((-sgn * 0.001 + 0.0025, 0.0095, 0.001)), 0.0011, M['eye_white'], seg=6, rings=4))
            Hd(tube_along('lash_line', [e + Vector((-0.018, 0.0, -0.001)), e + Vector((-0.005, 0.0072, lid + 0.0005)), e + Vector((0.009, 0.0068, lid + 0.0005)), e + Vector((0.019, 0.0, 0.0))], 0.0036, M['ink'], sides=5))
            Hd(tube_along('upper_lid', [e + Vector((-0.019, 0.0, 0.004)), e + Vector((0.0, 0.0062, lid + 0.004)), e + Vector((0.019, 0.0, 0.004))], 0.0058, HM['skin'], sides=6))
            # lower lid, bags, crow's feet and cheek hatching removed (R4): 0.3-0.9 px in a race, the
            # heaviest marks on the face in close-up - a few bold lines read as drawn instead
            br = on_face((sgn * 0.38, 1.0, (0.29, 0.3, 0.32)[v]), 0.01)
            bw = (0.0078, 0.007, 0.0058)[v]
            Hd(tube_along('brow', [br + Vector((-sgn * 0.027, 0.003, -0.011)), br + Vector((-sgn * 0.01, 0.004, -0.002)), br + Vector((sgn * 0.01, 0.0, 0.002)), br + Vector((sgn * 0.028, -0.01, -0.005))], bw, HM['beard'] if v == 1 else HM['hair'], sides=10))
            # deep nasolabial fold and cheek hatching (the comic ink of a weathered face)
            Hd(tube_along('smile_line', [on_face((sgn * 0.19, 1.0, -0.19), 0.002), on_face((sgn * 0.25, 1.0, -0.3), 0.002), on_face((sgn * 0.27, 1.0, -0.4), 0.002)], 0.0024, M['ink'], sides=4))
        if v == 0:  # one bold frown line, the tired detective only
            Hd(tube_along('frown_line', [on_face((-0.04, 1.0, 0.31), 0.002), on_face((-0.025, 1.0, 0.41), 0.002)], 0.0022, M['ink'], sides=4))
        if v == 0:  # Bigby's forehead scar: a cross of ink cuts
            sc = on_face((0.12, 1.0, 0.5), 0.0015)
            Hd(tube_along('scar', [sc + Vector((-0.012, 0, -0.008)), sc + Vector((0.012, -0.002, 0.008))], 0.0013, M['ink'], sides=3))
            Hd(tube_along('scar', [sc + Vector((-0.01, 0, 0.007)), sc + Vector((0.011, -0.002, -0.007))], 0.0013, M['ink'], sides=3))
            Hd(tube_along('scar', [sc + Vector((-0.005, 0.0, 0.011)), sc + Vector((0.004, -0.001, -0.011))], 0.0011, M['ink'], sides=3))
        # mouth, lower lip with its shadow
        Hd(tube_along('mouth', [mo + Vector((-0.024, -0.004, -0.002)), mo + Vector((-0.01, 0.0, -0.004)), mo + Vector((0.008, 0.0, -0.003)), mo + Vector((0.025, -0.004, 0.002))], 0.0019, M['ink'], sides=5))
        mz = (mo - Hc).z
        paint(lambda cc: cc.y > 0.06 and abs(cc.x) < 0.019 - 40.0 * (cc.z - (mz - 0.007)) ** 2 and mz - 0.013 < cc.z < mz - 0.002, HM['lips'])
        Hd(tube_along('lip_crease', [on_face((-0.1, 1.0, -0.6), 0.0015), on_face((0.0, 1.0, -0.62), 0.0015), on_face((0.1, 1.0, -0.6), 0.002)], 0.0022, M['ink'], sides=4))
        # Planar face normals (research R2: Guilty Gear Xrd's hand-edited normals, GDC 2015). The
        # cartoon shader steps light to shadow on the normal; a smooth sphere made the face all lit or
        # randomly blotched. Each skull vertex takes the normal of the nearest face of a coarse copy
        # of the same sculpted head, so the shadow edge runs along forehead, cheek and jaw planes.
        # Neighbouring planes differ by < 37 deg: under the crease-ink threshold, so no extra lines.
        from mathutils.bvhtree import BVHTree
        proxy = _sphere('head_proxy', (0, 0, 0), 0.1, HM['skin'], seg=14, rings=8)
        sculpt(proxy, jaw_w)
        tree = BVHTree.FromPolygons([v_.co.copy() for v_ in proxy.data.vertices], [tuple(p_.vertices) for p_ in proxy.data.polygons])
        pn = [p_.normal.copy() for p_ in proxy.data.polygons]
        H['bpy'].data.objects.remove(proxy, do_unlink=True)
        skull.data.update()
        normals = []
        for v_ in skull.data.vertices:
            hit = tree.find_nearest(v_.co)
            n_ = pn[hit[2]] if hit[2] is not None else v_.normal
            if n_.dot(v_.normal) < 0:
                n_ = -n_
            normals.append((n_ * 0.85 + v_.normal * 0.15).normalized())
        skull.data.normals_split_custom_set_from_vertices(normals)
        for o in head:
            if o.type != 'MESH':
                continue
            attr = o.data.color_attributes.new('Col', 'FLOAT_COLOR', 'POINT')
            for vi, v in enumerate(attr.data):
                c3 = pcol.get(vi, (1.0, 1.0, 1.0)) if o is skull else (1.0, 1.0, 1.0)
                v.color = (c3[0], c3[1], c3[2], 1.0)
            o.data.color_attributes.active_color = attr
        return head, HM

    head_pivot = neck_base + Vector((0, -0.005, 0.07))
    from mathutils import Matrix
    grow = Matrix.Translation(head_pivot) @ Matrix.Scale(1.08, 4) @ Matrix.Translation(-head_pivot)
    for v in range(3):
        parts_v, HM = build_head(v)
        # the head was ~4% small against its body (ANSUR II, research R7); a stylised head reads a
        # little large: +8% about the neck pivot
        for o in parts_v:
            o.matrix_world = grow @ o.matrix_world
        c.animated('DRIVER_HEAD' if v == 0 else 'DRIVER_HEAD_%d' % v, parts_v, head_pivot)
    eye = H['bpy'].data.objects.new('DRIVER_EYE', None)
    H['bpy'].context.scene.collection.objects.link(eye)
    eye.location = on_head((0, 1.0, 0.2), 0.004)
    c.eye = eye

    # --- arms (two-bone IK in the game) and hands on the rim
    R = 0.18
    tilt = (math.pi / 2 - c.wheel_tilt, 0, 0)
    ax = Vector((0, -math.cos(c.wheel_tilt), math.sin(c.wheel_tilt)))          # wheel axis, towards the driver
    up_w = Vector((0, math.sin(c.wheel_tilt), math.cos(c.wheel_tilt)))
    UPPER, FORE = 0.29, 0.28
    c.arm_lengths = (UPPER, FORE)
    for sgn, tag in ((-1, 'L'), (1, 'R')):
        shoulder = Vector((x + sgn * 0.2, ys - 0.17, zh - 0.27))
        grip = SW + Vector((sgn * R, 0, 0)) + up_w * 0.02
        wrist = grip + ax * 0.06 + Vector((-sgn * 0.02, 0, -0.02))
        elbow = shoulder + ((shoulder + wrist) / 2 + Vector((sgn * 0.08, 0, -0.1)) - shoulder).normalized() * UPPER
        # shaped sleeves (D-070): a round shoulder, the bicep, a taper to the elbow and to the knit cuff
        ua = [_limb('sleeve_upper', UPPER, [(0.0, 0.05), (0.1, 0.06), (0.35, 0.062), (0.6, 0.056), (0.88, 0.049), (1.0, 0.047)], M['jacket']),
              _sphere('deltoid', (0, 0, 0.02), 0.064, M['jacket'], scale=(1.0, 0.95, 1.1), seg=16, rings=10),
              box('sleeve_stripe', Vector((0.058, 0, UPPER * 0.5)), (0.006, 0.02, UPPER * 0.8), M['accent']),
              box('sleeve_seam', Vector((-0.057, 0, UPPER * 0.5)), (0.004, 0.004, UPPER * 0.75), M['ink'])]
        fa = [_limb('sleeve_fore', FORE * 0.7, [(0.0, 0.047), (0.2, 0.05), (0.5, 0.047), (0.85, 0.042), (1.0, 0.041)], M['jacket']),
              _sphere('elbow', (0, 0, 0), 0.049, M['jacket'], seg=14, rings=8),
              _limb('cuff', 0.045, [(0.0, 0.041), (0.15, 0.043), (0.85, 0.042), (1.0, 0.037)], M['jacket_dark']),
              box('cuff_seam', Vector((0, 0.037, FORE * 0.7 + 0.02)), (0.004, 0.004, 0.04), M['ink']),
              _limb('wrist', FORE * 0.3, [(0.0, 0.029), (0.5, 0.027), (1.0, 0.028)], M['skin'], sides=12)]
        fa[2].location = (0, 0, FORE * 0.7)
        fa[4].location = (0, 0, FORE * 0.7)
        c.bone('UPPER_ARM_' + tag, ua, shoulder, elbow - shoulder)
        c.bone('FOREARM_' + tag, fa, elbow, wrist - elbow)
        c.animated('HAND_' + tag, hand_on_rim(M, grip, sgn, ax, up_w), SW, tilt)
        g = H['bpy'].data.objects.new('GRIP_' + tag, None)
        H['bpy'].context.scene.collection.objects.link(g)
        g.location = wrist
        c.grips = getattr(c, 'grips', []) + [g]
    # the right hand on the gear knob and the left hand's middle finger: shown by the game
    knob = c.knob_empty.location.copy()
    c.animated('SHIFT_HAND', hand_on_knob(M, knob), knob)
    c.animated('TAUNT_HAND', hand_taunt(M, Vector((0, 0, 0))), Vector((0, 0, 0)))
    c.animated('POINT_HAND_L', hand_point(M), Vector((0, 0, 0)))
    c.animated('POINT_HAND_R', hand_point(M), Vector((0, 0, 0)))
    c.add(*items)


def _limb(name, length, prof, m, sides=16, flat=0.92):
    """A limb lofted along +Z from 0 to length; prof = [(t 0..1, radius)], both ends capped."""
    verts, faces = [], []
    for t, r in prof:
        for k in range(sides):
            a = 2 * math.pi * k / sides
            verts.append((math.cos(a) * r, math.sin(a) * r * flat, t * length))
    n = len(prof)
    for i in range(n - 1):
        for k in range(sides):
            a0, a1 = i * sides + k, i * sides + (k + 1) % sides
            faces.append((a0, a1, a1 + sides, a0 + sides))
    verts.append((0, 0, prof[0][0] * length))
    verts.append((0, 0, prof[-1][0] * length))
    c0, c1 = len(verts) - 2, len(verts) - 1
    for k in range(sides):
        faces.append((c0, (k + 1) % sides, k))
        faces.append((c1, (n - 1) * sides + k, (n - 1) * sides + (k + 1) % sides))
    o = H['link'](name, verts, faces, [m])
    H['fix_normals'](o)
    for p_ in o.data.polygons:
        p_.use_smooth = True
    return o


def _finger(M, pts, r, nail=True, m=None, tip=True):
    """A finger as a gloved tube with knuckles; the last segment bare skin (open-finger driving glove)."""
    tube_along = H['tube_along']
    m = m or M['glove']
    parts = [tube_along('finger', pts[:-1] + [pts[-2].lerp(pts[-1], 0.45)], r * 1.05, m, sides=8)]
    parts.append(tube_along('fingertip', [pts[-2].lerp(pts[-1], 0.4), pts[-1]], r * 0.95, M['skin'] if tip else m, sides=8))
    parts.append(_sphere('fingertip_end', pts[-1], r * 0.97, M['skin'] if tip else m, seg=8, rings=5))
    for k in range(1, len(pts) - 1):
        parts.append(_sphere('knuckle', pts[k], r * 1.12, m, seg=8, rings=5))
    return parts


def _hand_back(M, c, fwd, up, out, w=0.05, l=0.075):
    """Back of a gloved hand: a padded, slightly domed plate with a knuckle ridge, a vented strap and
    contrast stitching - the part of the hand the player actually sees."""
    box = H['box']
    parts = [_sphere('hand_body', c, 0.05, M['glove'], seg=16, rings=10)]
    o = parts[-1]
    o.rotation_mode = 'QUATERNION'
    # local axes: x = across the knuckles (up), y = along the hand (fwd), z = the back normal (out)
    import mathutils
    fwd = fwd.normalized()
    out = (out - fwd * out.dot(fwd)).normalized()
    up = fwd.cross(out)
    mtx = mathutils.Matrix((up, fwd, out)).transposed()
    o.rotation_quaternion = mtx.to_quaternion()
    o.scale = (w / 0.05 * 0.52, l / 0.05 * 0.55, 0.36)
    pad = _sphere('knuckle_pad', c + fwd * (l * 0.28) + out * 0.013, 0.05, M['glove_pad'], seg=12, rings=6)
    pad.rotation_mode = 'QUATERNION'
    pad.rotation_quaternion = o.rotation_quaternion
    pad.scale = (w / 0.05 * 0.42, 0.14, 0.12)
    parts.append(pad)
    strap = box('glove_strap', c - fwd * (l * 0.3) + out * 0.012, (0.02, 0.02, 0.02), M['accent'], 0.004)
    strap.rotation_mode = 'QUATERNION'
    strap.rotation_quaternion = o.rotation_quaternion
    strap.scale = (w * 1.9 / 0.02, 0.9, 0.35)
    parts.append(strap)
    return parts


def hand_on_rim(M, grip, sgn, ax, up_w):
    """Gloved hand wrapped round the rim at 9/3: back of the hand towards the driver, four fingers
    over the outside of the rim and curled round its front, the thumb hooked over the spoke."""
    out = Vector((sgn, 0, 0))
    parts = []
    back_c = grip + ax * 0.034 + out * 0.012
    parts += _hand_back(M, back_c, (up_w * 0.2 - ax * 0.2 + out * 1.0).normalized(), up_w, (ax + out * 0.35).normalized())
    for k in range(4):
        o = up_w * (0.027 - 0.018 * k)
        L = (0.95, 1.0, 0.95, 0.82)[k]
        p0 = grip + ax * 0.028 + out * 0.03 + o
        p1 = grip + ax * 0.006 + out * 0.036 * L + o
        p2 = grip - ax * 0.026 * L + out * 0.016 + o
        p3 = grip - ax * 0.024 * L - out * 0.012 * L + o
        parts += _finger(M, [p0, p1, p2, p3], 0.0105 if k < 3 else 0.0092)
    t0 = grip + ax * 0.03 - out * 0.004 + up_w * 0.03
    parts += _finger(M, [t0, grip + ax * 0.024 - out * 0.026 + up_w * 0.018, grip + ax * 0.008 - out * 0.04 + up_w * 0.008], 0.0118)
    parts.append(_sphere('thumb_pad', grip + ax * 0.03 - out * 0.008 + up_w * 0.018, 0.02, M['glove'], scale=(0.8, 0.8, 1.1), seg=10, rings=6))
    return parts


def hand_on_knob(M, knob):
    """Right hand over the gear knob: palm cupping the top, fingers wrapped round the front."""
    parts = _hand_back(M, knob + Vector((0.004, -0.012, 0.05)), Vector((0, 1, -0.15)).normalized(), Vector((1, 0, 0)), Vector((0, 0.15, 1)).normalized())
    for k in range(4):
        dx = -0.03 + 0.02 * k
        p0 = knob + Vector((dx, 0.012, 0.056))
        parts += _finger(M, [p0, knob + Vector((dx, 0.038, 0.042)), knob + Vector((dx * 0.9, 0.042, 0.006)), knob + Vector((dx * 0.8, 0.022, -0.02))], 0.0105)
    parts += _finger(M, [knob + Vector((-0.04, -0.02, 0.035)), knob + Vector((-0.046, 0.0, 0.01)), knob + Vector((-0.036, 0.02, -0.01))], 0.012)
    return parts


def hand_taunt(M, o):
    """Left fist with the middle finger up, wrist at the origin, pointing up (+Z), back of the hand
    facing out of the car (-X)."""
    parts = _hand_back(M, o + Vector((-0.004, 0, 0.05)), Vector((0, 0, 1)), Vector((0, 1, 0)), Vector((-1, 0, 0)), w=0.05, l=0.07)
    for k, dy in enumerate((0.03, 0.01, -0.01, -0.03)):
        if k == 1:
            parts += _finger(M, [o + Vector((0.004, dy, 0.08)), o + Vector((0.004, dy, 0.12)), o + Vector((0.004, dy, 0.165))], 0.011)
        else:
            parts += _finger(M, [o + Vector((-0.004, dy, 0.085)), o + Vector((0.02, dy, 0.09)), o + Vector((0.03, dy, 0.065))], 0.0105)
    parts += _finger(M, [o + Vector((0.01, -0.045, 0.05)), o + Vector((0.03, -0.035, 0.065))], 0.012)
    parts.append(H['tube_along']('taunt_wrist', [o + Vector((0, 0, -0.05)), o + Vector((0, 0, 0.02))], 0.03, M['skin'], sides=10))
    parts.append(H['cyl']('taunt_cuff', o + Vector((0, 0, -0.07)), 0.042, 0.05, M['jacket_dark'], rot=(0, 0, 0), verts=12))
    return parts


def hand_point(M):
    """Hand with the index finger out, fingertip at the origin pointing +Y (forward), wrist behind."""
    parts = _hand_back(M, Vector((0, -0.11, 0.0)), Vector((0, 1, 0)), Vector((1, 0, 0)), Vector((0, 0, 1)), w=0.05, l=0.075)
    parts += _finger(M, [Vector((0.0, -0.075, 0.0)), Vector((0.0, -0.04, 0.0)), Vector((0.0, -0.012, 0.0))], 0.0105)
    for k, dx in enumerate((0.02, 0.035, 0.045)):
        parts += _finger(M, [Vector((dx - 0.02, -0.08, -0.005)), Vector((dx - 0.02, -0.055, -0.03)), Vector((dx - 0.02, -0.08, -0.04))], 0.01)
    parts += _finger(M, [Vector((-0.035, -0.11, -0.005)), Vector((-0.03, -0.07, -0.02))], 0.012)
    parts.append(H['tube_along']('point_wrist', [Vector((0, -0.2, -0.02)), Vector((0, -0.14, -0.012))], 0.03, M['skin'], sides=10))
    parts.append(H['cyl']('point_cuff', Vector((0, -0.215, -0.022)), 0.042, 0.05, M['jacket_dark'], rot=(math.pi / 2, 0, 0), verts=12))
    return parts
