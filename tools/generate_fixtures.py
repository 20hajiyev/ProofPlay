"""M0 import fixtures. Run: blender --background --factory-startup --python tools/generate_fixtures.py
Blender authoring convention (D-002): metres, Z up, vehicle nose +Y, so vehicle left is -X.
"""
import bpy, json, hashlib, math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'assets' / 'fixtures'
OUT.mkdir(parents=True, exist_ok=True)


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def material(name, rgba):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    m.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = rgba
    return m


def cube(name, loc, size, mat):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc)
    o = bpy.context.object
    o.name = name
    o.scale = size
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    o.data.materials.append(mat)
    return o


def export(path):
    bpy.ops.export_scene.gltf(filepath=str(path), export_format='GLB', export_yup=True, export_apply=True)


def axis_fixture():
    reset()
    red, green, blue, grey = (material(n, c) for n, c in [('X_red', (1, 0, 0, 1)), ('Y_green', (0, 1, 0, 1)), ('Z_blue', (0, 0, 1, 1)), ('grey', (.5, .5, .5, 1))])
    cube('CUBE_1M', (0, 0, .5), (1, 1, 1), grey)
    cube('MARK_POS_X', (2, 0, .5), (.3, .3, .3), red)
    cube('MARK_POS_Y', (0, 2, .5), (.3, .3, .3), green)
    cube('MARK_POS_Z', (0, 0, 2.5), (.3, .3, .3), blue)
    return {'MARK_POS_X': [2, 0, .5], 'MARK_POS_Y': [0, 2, .5], 'MARK_POS_Z': [0, 0, 2.5], 'CUBE_1M': [0, 0, .5]}


def vehicle_fixture():
    reset()
    body, nose, left, tire = (material(n, c) for n, c in [('body', (.6, .6, .6, 1)), ('nose', (1, .8, 0, 1)), ('left', (0, .4, 1, 1)), ('tire', (.02, .02, .02, 1))])
    cube('VEH_BODY', (0, 0, .6), (1.8, 4.0, .6), body)
    cube('VEH_NOSE_MARK', (0, 2.2, .6), (.4, .4, .4), nose)
    cube('VEH_LEFT_MARK', (-1.1, 0, .6), (.2, .8, .2), left)
    pivot = bpy.data.objects.new('WHEEL_PIVOT_FL', None)
    bpy.context.collection.objects.link(pivot)
    pivot.location = (-.85, 1.3, .36)
    bpy.ops.mesh.primitive_cylinder_add(vertices=24, radius=.36, depth=.25, location=(-.85, 1.3, .36), rotation=(0, math.pi / 2, 0))
    wheel = bpy.context.object
    wheel.name = 'TIRE_FL'
    wheel.data.materials.append(tire)
    # Spin marker lets a viewer see wheel rotation direction.
    cube('TIRE_FL_SPIN_MARK', (-.99, 1.3, .66), (.04, .08, .08), nose)
    for o in (wheel, bpy.data.objects['TIRE_FL_SPIN_MARK']):
        o.parent = pivot
        o.matrix_parent_inverse = pivot.matrix_world.inverted()
    return {'VEH_NOSE_MARK': [0, 2.2, .6], 'VEH_LEFT_MARK': [-1.1, 0, .6], 'WHEEL_PIVOT_FL': [-.85, 1.3, .36]}


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


records = []
for name, build in [('FIXTURE_AXES', axis_fixture), ('FIXTURE_VEHICLE_AXES', vehicle_fixture)]:
    markers = build()
    glb = OUT / f'{name}.glb'
    export(glb)
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT / f'{name}.blend'))
    records.append({'asset_id': name, 'category': 'fixture', 'status': 'built',
                    'source_paths': [f'assets/fixtures/{name}.blend'], 'runtime_paths': [f'assets/fixtures/{name}.glb'],
                    'sha256': sha(glb), 'blender_marker_positions_m': markers,
                    'provenance': {'generator': 'tools/generate_fixtures.py', 'blender': bpy.app.version_string, 'external_inputs': []}})
(OUT / 'manifest_fragment.json').write_text(json.dumps({'schema_version': 1, 'assets': records}, indent=2), encoding='utf8')
print('FIXTURES_COMPLETE', [r['asset_id'] for r in records], flush=True)
