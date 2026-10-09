"""The driver character as its own model (D-076). Run:
    blender --background --factory-startup --python tools/generate_driver.py

Owner (2026-10-02): "make the driver separately, not as one with the car; and let the driver's
character design be customisable". One character file is shared by every car: the game sits it on
the car's DRIVER_ANCHOR (the hip), poses the arms onto the steering wheel and the legs onto the
pedals with two-bone IK, and recolours skin / hair / jacket from the player's choices.

Output: assets/characters/driver/driver.glb plus model-sheet renders. Every pivot is a top-level
object in a "standard seat" frame: hip at (-0.37, 0, 0), the wheel and gear knob where every cabin
puts them relative to the hip (car_cabin.HEAD_H keeps that fixed).
"""
import math
import sys
from pathlib import Path

import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / 'tools'
sys.path.insert(0, str(TOOLS))

# the car generator's mesh helpers and palette, without running its build loop
_src = (TOOLS / 'generate_tuner_cars.py').read_text(encoding='utf-8')
G = {'__name__': 'gen_helpers', '__file__': str(TOOLS / 'generate_tuner_cars.py')}
exec(compile(_src[:_src.index('car_cabin.setup(dict(')], 'generate_tuner_cars.py', 'exec'), G)
import car_cabin  # noqa: E402 (after the path is set)

OUT = ROOT / 'assets' / 'characters' / 'driver'


class SeatFrame:
    """Stands in for a car: the seat, wheel and knob of the standard frame and the pivot lists the
    car generator would collect."""

    def __init__(self, M):
        self.id = 'DRIVER'
        self.M = M
        HEAD_H = car_cabin.HEAD_H
        # hip = (-0.37, ys - 0.02, zc + 0.12) = (-0.37, 0, 0)
        self.seat = (0.02, -0.12, -0.12 + HEAD_H)
        ys, zc, zh = self.seat
        E = Vector((-0.37, ys - 0.12, zh + 0.03))
        self.wheel_centre = Vector((-0.37, E.y + 0.55, E.z - 0.36))
        self.wheel_tilt = 0.42
        self.floor = zc - 0.14
        knob = bpy.data.objects.new('GEAR_KNOB_REF', None)
        bpy.context.scene.collection.objects.link(knob)
        knob.location = Vector((0.0, 0.40, 0.27))
        self.knob_empty = knob
        self.fixed, self.special, self.bones, self.markers, self.grips = [], {}, [], [], []

    def add(self, *objs):
        self.fixed.extend(objs)

    def animated(self, name, objs, loc, rot=(0, 0, 0)):
        self.special.setdefault(name, ([], Vector(loc), rot))[0].extend(objs)

    def bone(self, name, objs, start, direction):
        self.bones.append((name, objs, Vector(start), Vector(direction)))


def build():
    G['reset']()
    M = G['palette']((0.5, 0.5, 0.5), (0.95, 0.55, 0.1), (0.8, 0.8, 0.8), suit=(0.75, 0.1, 0.08))
    car_cabin.setup(dict(bpy=bpy, box=G['box'], cyl=G['cyl'], torus=G['torus'], tube_along=G['tube_along'], link=G['link'],
                         fix_normals=G['fix_normals'], mat=G['mat'], mirror_quad=G['mirror_quad']))
    car_cabin.materials(M, (0.75, 0.1, 0.08), 'D01')
    c = SeatFrame(M)
    car_cabin.driver(c, seed=7)
    join = G['join']
    # pivots as top-level objects (the game re-parents them under the car's BODY)
    for name, (objs, loc, rot) in c.special.items():
        pivot = bpy.data.objects.new(name, None)
        bpy.context.scene.collection.objects.link(pivot)
        pivot.location = loc
        pivot.rotation_euler = rot
        bpy.context.view_layer.update()
        mesh = join(objs, 'MESH_' + name)
        mesh.parent = pivot
        mesh.matrix_parent_inverse = pivot.matrix_world.inverted()
    for name, objs, start, direction in c.bones:
        pivot = bpy.data.objects.new(name, None)
        bpy.context.scene.collection.objects.link(pivot)
        pivot.location = start
        pivot.rotation_mode = 'QUATERNION'
        pivot.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(direction.normalized())
        bpy.context.view_layer.update()
        mesh = join(objs, 'MESH_' + name)
        mesh.parent = pivot
    if c.fixed:  # anything not on a pivot rides with the hips
        hips = bpy.data.objects['DRIVER_HIPS']
        mesh = join(c.fixed, 'MESH_DRIVER_EXTRA')
        bpy.context.view_layer.update()
        mesh.parent = hips
        mesh.matrix_parent_inverse = hips.matrix_world.inverted()
    # the grips live in the hands' frame - the same frame as a car's STEER_PIVOT (wheel centre, tilt)
    for g in c.grips:
        hand = bpy.data.objects['HAND_' + g.name[-1]]
        bpy.context.view_layer.update()
        wm = g.matrix_world.copy()
        g.parent = hand
        g.matrix_world = wm
    # the eye stays with the car (DRIVER_EYE there, same formula); drop the character's copy and helpers
    for n in ('DRIVER_EYE', 'GEAR_KNOB_REF'):
        if bpy.data.objects.get(n):
            bpy.data.objects.remove(bpy.data.objects[n])
    return c


def review():
    sc = bpy.context.scene
    world = bpy.data.worlds.new('w')
    sc.world = world
    world.use_nodes = True
    world.node_tree.nodes['Background'].inputs[0].default_value = (0.55, 0.6, 0.7, 1)
    bpy.ops.object.light_add(type='SUN')
    bpy.context.object.data.energy = 4
    bpy.context.object.rotation_euler = (0.7, 0.3, 0.8)
    sc.render.engine = 'BLENDER_EEVEE_NEXT'
    sc.render.resolution_x, sc.render.resolution_y = 1100, 680
    e = Vector((-0.37, 0.02 - 0.21 + 0.1, -0.12 + car_cabin.HEAD_H + 0.02))
    heads = ['MESH_DRIVER_HEAD', 'MESH_DRIVER_HEAD_1', 'MESH_DRIVER_HEAD_2']
    hidden_hands = {'MESH_SHIFT_HAND', 'MESH_TAUNT_HAND', 'MESH_POINT_HAND_L', 'MESH_POINT_HAND_R'}
    views = []
    for hv in range(3):
        views.append(('portrait%d' % hv, hv, e + Vector((0.12, 0.75, -0.08)), e + Vector((0, -0.08, -0.12)), 60))
        views.append(('profile%d' % hv, hv, e + Vector((0.8, -0.05, -0.05)), e + Vector((0, -0.08, -0.08)), 60))
    views.append(('body', 0, Vector((1.6, 1.9, 0.9)), Vector((-0.37, 0.35, 0.05)), 30))
    views.append(('body_side', 0, Vector((2.4, 0.3, 0.3)), Vector((-0.37, 0.3, 0.05)), 30))
    for name, hv, loc, aim, lens in views:
        for o in bpy.context.scene.objects:
            if o.type == 'MESH':
                o.hide_render = (o.name in heads and o.name != heads[hv]) or o.name in hidden_hands
        bpy.ops.object.camera_add(location=loc)
        cam = bpy.context.object
        cam.rotation_euler = (aim - cam.location).to_track_quat('-Z', 'Y').to_euler()
        cam.data.lens = lens
        cam.data.clip_start = 0.02
        sc.camera = cam
        sc.render.filepath = str(OUT / f'driver_{name}.png')
        bpy.ops.render.render(write_still=True)


c = build()
OUT.mkdir(parents=True, exist_ok=True)
bpy.ops.object.select_all(action='SELECT')
for o in bpy.context.selected_objects:
    if o.type in ('CAMERA', 'LIGHT'):
        o.select_set(False)
bpy.ops.export_scene.gltf(filepath=str(OUT / 'driver.glb'), export_format='GLB', use_selection=True, export_apply=True,
                          export_yup=True, export_vertex_color='ACTIVE')
faces = sum(len(o.data.polygons) for o in bpy.data.objects if o.type == 'MESH')
pivots = sorted(o.name for o in bpy.data.objects if o.type == 'EMPTY' and o.parent is None)
print('DRIVER_COMPLETE faces', faces, 'pivots', len(pivots), pivots, flush=True)
review()
