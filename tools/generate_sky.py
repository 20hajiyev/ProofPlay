"""Toon sky kit (D-057): anime cumulus clouds, a sun disc with halo, a cratered moon and a star
field. The game places them around the camera and recolours them per time of day; the cartoon
shader and ink outlines give the clouds their cel look. Original procedural geometry.

Run: blender --background --factory-startup --python tools/generate_sky.py
Output: assets/environment/sky/sky_kit.glb with objects CLOUD_0..CLOUD_5, SUN, SUN_HALO, MOON, STARS.
Materials whose name holds "NoInk" draw no outline in the game (tiny or glowing things).
"""
import bpy, bmesh, math, random
from pathlib import Path
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'assets' / 'environment' / 'sky'
OUT.mkdir(parents=True, exist_ok=True)
rnd = random.Random(5731)
bpy.ops.wm.read_factory_settings(use_empty=True)


def mat(name, rgb, emit=0.0, alpha=1.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    p = m.node_tree.nodes['Principled BSDF']
    p.inputs['Base Color'].default_value = (*rgb, 1)
    p.inputs['Roughness'].default_value = 0.9
    if emit:
        p.inputs['Emission Color'].default_value = (*rgb, 1)
        p.inputs['Emission Strength'].default_value = emit
    if alpha < 1:
        p.inputs['Alpha'].default_value = alpha
        m.surface_render_method = 'BLENDED'
    return m


CLOUD = mat('Toon Cloud', (0.95, 0.96, 1.0))
SUN = mat('Sky Sun NoInk', (1.0, 0.92, 0.7), emit=12.0)
HALO = mat('Sky Sun Halo NoInk', (1.0, 0.8, 0.5), emit=3.0, alpha=0.25)
MOON = mat('Sky Moon', (0.86, 0.88, 0.95), emit=1.6)
CRATER = mat('Sky Moon Crater NoInk', (0.62, 0.65, 0.75), emit=1.1)
STAR = mat('Sky Star NoInk', (1.0, 1.0, 1.0), emit=8.0)


def join(objs, name):
    bpy.ops.object.select_all(action='DESELECT')
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    bpy.ops.object.join()
    o = bpy.context.object
    o.name = name
    return o


def cloud(i):
    """A cumulus: a flat-bottomed cluster of puffs, big in the middle, smaller at the ends."""
    width = rnd.uniform(140, 240)
    n = rnd.randint(7, 11)
    puffs = []
    for k in range(n):
        t = (k / (n - 1)) * 2 - 1                     # -1..1 along the cloud
        r = (1 - 0.55 * abs(t)) * rnd.uniform(24, 36)
        x = t * width / 2
        y = rnd.uniform(-18, 18)
        z = r * 0.55 + rnd.uniform(0, 10) * (1 - abs(t))
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=2, radius=r, location=(x, y, z))
        puffs.append(bpy.context.object)
    for k in range(n // 2):                            # a second layer of puffs on top
        t = rnd.uniform(-0.6, 0.6)
        r = rnd.uniform(18, 28)
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=2, radius=r, location=(t * width / 2, rnd.uniform(-10, 10), 34 + r * 0.4))
        puffs.append(bpy.context.object)
    o = join(puffs, f'CLOUD_{i}')
    # flat underside, the classic anime cumulus base
    for v in o.data.vertices:
        if v.co.z < 4:
            v.co.z = 4 + (v.co.z - 4) * 0.08
    o.data.materials.clear()
    o.data.materials.append(CLOUD)
    for p in o.data.polygons:
        p.use_smooth = True
    o.location = (0, 0, 0)
    return o


for i in range(6):
    cloud(i)

# sun and halo as spheres: a sphere reads as a disc from every view, no billboarding needed
bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=16, radius=30, location=(0, 0, 0))
sun = bpy.context.object; sun.name = 'SUN'; sun.data.materials.append(SUN)
bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=16, radius=58, location=(0, 0, 0))
h = bpy.context.object; h.name = 'SUN_HALO'; h.data.materials.append(HALO)
bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=16, radius=26, location=(0, 0, 0))
moon = bpy.context.object; moon.name = 'MOON'; moon.data.materials.append(MOON)
craters = []
for k in range(9):
    a = rnd.uniform(0, 2 * math.pi); d = rnd.uniform(0, 17)
    rr = rnd.uniform(3, 7)
    bpy.ops.mesh.primitive_circle_add(vertices=16, radius=rr, fill_type='NGON', location=(math.cos(a) * d, -26.3 + d * d / 60, math.sin(a) * d), rotation=(math.pi / 2, 0, 0))
    craters.append(bpy.context.object)
c = join(craters, 'MOON_CRATERS'); c.data.materials.clear(); c.data.materials.append(CRATER)
c.parent = moon

# star field: small octahedra on the upper dome, facing all ways (octahedra need no orientation)
bm = bmesh.new()
for k in range(700):
    az = rnd.uniform(0, 2 * math.pi)
    el = math.asin(rnd.uniform(0.08, 1.0))
    R = 580
    ctr = Vector((math.cos(az) * math.cos(el) * R, math.sin(az) * math.cos(el) * R, math.sin(el) * R))
    s = rnd.choice([0.9, 1.2, 1.2, 1.6, 2.4])
    ret = bmesh.ops.create_icosphere(bm, subdivisions=0, radius=s)
    bmesh.ops.translate(bm, vec=ctr, verts=ret['verts'])
me = bpy.data.meshes.new('stars'); bm.to_mesh(me); bm.free()
stars = bpy.data.objects.new('STARS', me); bpy.context.scene.collection.objects.link(stars); me.materials.append(STAR)

bpy.ops.object.select_all(action='SELECT')
bpy.ops.export_scene.gltf(filepath=str(OUT / 'sky_kit.glb'), export_format='GLB', use_selection=True, export_apply=True, export_yup=True)
print('SKY_COMPLETE', sum(len(o.data.polygons) for o in bpy.context.scene.objects if o.type == 'MESH'), flush=True)
