"""Stage 1 of the local real-car import (D-035): measure and preview every downloaded model.
Run: blender --background --factory-startup --python tools/local_cars_probe.py [-- car_key ...]

For each source in assets/local_only/cars/cars.json: import, lay the long axis along +Y, scale to
the real length, put the lowest point on z=0, estimate the four wheel centres from the geometry,
and render side + top views with the estimated wheels drawn as rings. The orientation (which
end is the nose) is confirmed by looking at the renders, then written back to cars.json.
Output: assets/local_only/cars/probe/<key>.json and <key>.png
"""
import bpy, json, math, sys
from pathlib import Path
from mathutils import Vector, Matrix

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / 'assets' / 'local_only' / 'cars'
OUT = BASE / 'probe'
OUT.mkdir(exist_ok=True)


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


sys.path.insert(0, str(ROOT / 'tools'))
from local_cars_common import load, drop_junk, world_verts, normalise, estimate_wheels  # noqa: E402


def ring(name, centre, r):
    bpy.ops.mesh.primitive_torus_add(major_radius=r, minor_radius=0.025, location=centre, rotation=(0, math.pi / 2, 0))
    o = bpy.context.object
    o.name = name
    m = bpy.data.materials.new('marker')
    m.use_nodes = True
    p = m.node_tree.nodes['Principled BSDF']
    p.inputs['Emission Color'].default_value = (1, 0, 1, 1)
    p.inputs['Emission Strength'].default_value = 5
    o.data.materials.append(m)


def render(path, dims):
    sc = bpy.context.scene
    world = bpy.data.worlds.new('w')
    sc.world = world
    world.use_nodes = True
    world.node_tree.nodes['Background'].inputs[0].default_value = (.5, .55, .6, 1)
    world.node_tree.nodes['Background'].inputs[1].default_value = 1.0
    bpy.ops.object.light_add(type='SUN', location=(0, 0, 10))
    bpy.context.object.data.energy = 3
    bpy.context.object.rotation_euler = (0.6, 0.2, 0.4)
    sc.render.engine = 'BLENDER_EEVEE_NEXT'
    sc.render.resolution_x = 900
    sc.render.resolution_y = 900
    sc.render.image_settings.file_format = 'PNG'
    tiles = []
    # side (camera on -X looking +X: nose +Y appears on the LEFT), top (nose +Y appears at the TOP)
    for tag, loc, rot in [('side', (-12, 0, dims['H'] / 2), (math.pi / 2, 0, -math.pi / 2)), ('top', (0, 0, 12), (0, 0, 0))]:
        bpy.ops.object.camera_add(location=loc, rotation=rot)
        cam = bpy.context.object
        cam.data.type = 'ORTHO'
        cam.data.ortho_scale = max(dims['L'], 2) * 1.15
        sc.camera = cam
        p = OUT / f'_{tag}.png'
        sc.render.filepath = str(p)
        bpy.ops.render.render(write_still=True)
        tiles.append(p)
        bpy.data.objects.remove(cam, do_unlink=True)
    imgs = [bpy.data.images.load(str(t)) for t in tiles]
    w, h = imgs[0].size
    out = bpy.data.images.new('sheet', w * 2, h)
    px = [0.0] * (w * 2 * h * 4)
    for k, im in enumerate(imgs):
        src = list(im.pixels)
        for row in range(h):
            dst = (row * w * 2 + k * w) * 4
            px[dst:dst + w * 4] = src[row * w * 4:(row + 1) * w * 4]
    out.pixels = px
    out.filepath_raw = str(path)
    out.file_format = 'PNG'
    out.save()


cfg = json.loads((BASE / 'cars.json').read_text(encoding='utf-8'))
only = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
for key, car in cfg.items():
    if only and key not in only:
        continue
    reset()
    meshes = drop_junk(load(BASE / car['source']))
    tris = sum(len(o.data.polygons) for o in meshes)
    s = normalise(meshes, car['length_m'], car.get('yaw_deg', 0))
    wheels, dims = estimate_wheels(meshes)
    for n, wdat in wheels.items():
        if wdat:
            ring('W_' + n, (wdat['x_outer'] * (-1 if n[1] == 'L' else 1), wdat['y'], wdat['z']), wdat['r'])
    render(OUT / f'{key}.png', dims)
    info = {'key': key, 'scale_applied': s, 'dims': dims, 'wheels': wheels, 'mesh_objects': len(meshes), 'polygons': tris,
            'object_names': sorted({o.name for o in meshes})[:60]}
    (OUT / f'{key}.json').write_text(json.dumps(info, indent=1), encoding='utf-8')
    print('PROBED', key, json.dumps({'dims': dims, 'polys': tris, 'wheels': wheels}), flush=True)
