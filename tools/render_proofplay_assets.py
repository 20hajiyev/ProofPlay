"""Renders the ProofPlay dashboard art from the game's own assets (Blender, headless):
  - track_<ID>.png: a true top-down orthographic shot of the track scene, plus track_<ID>.json
    with the world bounds, so race paths overlay it exactly (game x = Blender x, game z = Blender y)
  - car_<ID>_top.png: a transparent top-view sprite (nose up) for the moving cars on the map
  - car_<ID>_hero.png: a transparent 3/4 view for the skill card

    .tools/blender-4.5.14-windows-x64/blender.exe -b -P tools/render_proofplay_assets.py [-- --cars-only | --track=ID]
"""
import json
import math
import pathlib
import sys

import bpy
from mathutils import Vector

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT / "proofplay/dashboard/assets"
def discover_tracks():
    """Every content/tracks/<ID>_<stem>.json with a Blender scene next to its visual asset."""
    out = {}
    for f in sorted((ROOT / "content/tracks").glob("*.json")):
        d = json.loads(f.read_text(encoding="utf-8"))
        folder = pathlib.Path(d.get("visual_asset", "")).parent.name
        if d.get("id") and (ROOT / "assets/tracks" / folder / f"{folder}.blend").exists():
            out[d["id"]] = (folder, f.name)
    return out
CARS = ["D01", "D02", "D03", "D04", "D05"]


def setup_render(res, transparent):
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_EEVEE_NEXT"
    sc.render.resolution_x, sc.render.resolution_y = res
    sc.render.resolution_percentage = 100
    sc.render.film_transparent = transparent
    sc.render.image_settings.file_format = "PNG"
    sc.render.image_settings.color_mode = "RGBA" if transparent else "RGB"
    if sc.world is None:
        sc.world = bpy.data.worlds.new("World")
    sc.world.color = (0.55, 0.55, 0.58)
    return sc


def add_camera(location, target, ortho_scale=None, lens=None):
    bpy.ops.object.camera_add(location=location)
    cam = bpy.context.object
    cam.rotation_euler = (Vector(target) - cam.location).to_track_quat("-Z", "Y").to_euler()
    if ortho_scale:
        cam.data.type = "ORTHO"
        cam.data.ortho_scale = ortho_scale
    if lens:
        cam.data.lens = lens
    cam.data.clip_end = 10000
    bpy.context.scene.camera = cam
    return cam


def add_sun(energy=3.0, rotation=(0.5, -0.4, -0.6)):
    bpy.ops.object.light_add(type="SUN", location=(0, 0, 100))
    sun = bpy.context.object
    sun.rotation_euler = rotation
    sun.data.energy = energy
    return sun


def render(path):
    bpy.context.scene.render.filepath = str(path)
    bpy.ops.render.render(write_still=True)


def render_track(track_id, folder, definition):
    bpy.ops.wm.open_mainfile(filepath=str(ROOT / "assets/tracks" / folder / f"{folder}.blend"))
    line = json.loads((ROOT / "content/tracks" / definition).read_text(encoding="utf-8"))["centerline"]
    xs, zs = [p[0] for p in line], [p[1] for p in line]
    pad = 260.0  # wide enough that zoomed-in corners near the edge still have ground under them
    x0, x1, z0, z1 = min(xs) - pad, max(xs) + pad, min(zs) - pad, max(zs) + pad
    w, h = x1 - x0, z1 - z0
    res_x = 1600
    res_y = int(round(res_x * h / w))
    setup_render((res_x, res_y), transparent=False)
    # Straight down; Blender ortho_scale spans the wider image side.
    cam = add_camera(((x0 + x1) / 2, (z0 + z1) / 2, 900), ((x0 + x1) / 2, (z0 + z1) / 2, 0), ortho_scale=max(w, h))
    cam.rotation_euler = (0.0, 0.0, 0.0)
    add_sun()
    render(OUT / f"track_{track_id}.png")
    # Image top row is +z (Blender +y); the dashboard maps z1 to the top edge.
    (OUT / f"track_{track_id}.json").write_text(json.dumps({"x0": x0, "x1": x1, "z0": z0, "z1": z1, "width": res_x, "height": res_y}), encoding="utf-8")


def car_bounds():
    lo, hi = Vector((1e9, 1e9, 1e9)), Vector((-1e9, -1e9, -1e9))
    for o in bpy.context.scene.objects:
        if o.type != "MESH":
            continue
        for c in o.bound_box:
            p = o.matrix_world @ Vector(c)
            lo = Vector(map(min, lo, p))
            hi = Vector(map(max, hi, p))
    return lo, hi


def show_stock_only(car_id):
    """The GLB carries every garage part; keep the stock variant of each slot, hide weapons."""
    slots = json.loads((ROOT / "content/customization" / f"{car_id}.json").read_text(encoding="utf-8"))["slots"]
    for o in bpy.context.scene.objects:
        name = o.name.split(".")[0]
        hide = name.startswith("WEAPON")
        if name.startswith("PART_"):
            rest = name[len("PART_"):]
            for slot, spec in slots.items():
                for variant in spec["variants"]:
                    if rest == f"{slot}_{variant}" or rest.startswith(f"{slot}_{variant}_"):
                        hide = variant != spec["stock"]
        o.hide_render = hide


def render_car(car_id):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(ROOT / "assets/vehicles_toon" / car_id / f"{car_id}.glb"))
    show_stock_only(car_id)
    lo, hi = car_bounds()
    c = (lo + hi) / 2
    size = hi - lo
    length = max(size.x, size.y)
    # Top sprite: nose up in the image. The cars' nose points to Blender +Y after import
    # (a 3/4 shot from -Y showed the tail), so an unrotated top camera already has it up.
    nose_along_y = size.y >= size.x
    setup_render((256, 512) if nose_along_y else (512, 256), transparent=True)
    cam = add_camera((c.x, c.y, c.z + 50), (c.x, c.y, c.z), ortho_scale=length * 1.06)
    cam.rotation_euler = (0.0, 0.0, 0.0 if nose_along_y else -math.pi / 2)
    add_sun(4.0, (0.3, -0.2, 0.0))
    render(OUT / f"car_{car_id}_top.png")
    # 3/4 hero view for the card.
    setup_render((900, 560), transparent=True)
    eye = c + Vector((length * 1.35, length * 1.55, length * 0.75))  # front 3/4
    add_camera(tuple(eye), tuple(c), lens=55)
    render(OUT / f"car_{car_id}_hero.png")


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    import sys
    only_cars = "--cars-only" in sys.argv
    only = [a.split("=", 1)[1] for a in sys.argv if a.startswith("--track=")]
    for track_id, (folder, definition) in discover_tracks().items():
        if not only_cars and (not only or track_id in only):
            render_track(track_id, folder, definition)
    if only:
        return
    for car_id in CARS:
        render_car(car_id)
    print("proofplay assets ->", OUT)


main()
