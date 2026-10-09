"""Review renders of the imported local cars (D-035): the exported game GLBs, front 3/4 view,
lit by the CC0 HDRI so paint and glass show real reflections.
Run: blender --background --factory-startup --python tools/local_cars_review.py [-- key ...]
Output: assets/local_only/cars/review/<key>.png
"""
import bpy, math, sys
from pathlib import Path
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / 'assets' / 'local_only' / 'cars'
OUT = BASE / 'review'
OUT.mkdir(exist_ok=True)
HDRI = ROOT / 'assets' / 'materials' / 'polyhaven' / 'industrial_sunset_02_puresky_2k.hdr'

only = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
for glb in sorted((BASE / 'out').glob('L*.glb')):
    key = glb.stem
    if only and key not in only:
        continue
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(glb))
    sc = bpy.context.scene
    world = bpy.data.worlds.new('w'); sc.world = world; world.use_nodes = True
    nt = world.node_tree; env = nt.nodes.new('ShaderNodeTexEnvironment'); env.image = bpy.data.images.load(str(HDRI))
    nt.links.new(env.outputs['Color'], nt.nodes['Background'].inputs['Color']); nt.nodes['Background'].inputs['Strength'].default_value = 1.0
    bpy.ops.mesh.primitive_plane_add(size=40)
    m = bpy.data.materials.new('floor'); m.use_nodes = True; m.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = (.08, .08, .09, 1)
    m.node_tree.nodes['Principled BSDF'].inputs['Roughness'].default_value = .6; bpy.context.object.data.materials.append(m)
    # Game GLBs are Y-up glTF -> Blender Z-up, nose +Y (D-002). Camera front-left 3/4.
    bpy.ops.object.camera_add(location=(-5.2, 6.2, 2.0)); cam = bpy.context.object
    cam.rotation_euler = (Vector((0, 0.3, 0.6)) - cam.location).to_track_quat('-Z', 'Y').to_euler(); cam.data.lens = 50; sc.camera = cam
    sc.render.engine = 'BLENDER_EEVEE_NEXT'; sc.render.resolution_x = 800; sc.render.resolution_y = 500
    sc.render.filepath = str(OUT / f'{key}.png'); sc.render.image_settings.file_format = 'PNG'
    bpy.ops.render.render(write_still=True)
    print('REVIEWED', key, flush=True)
