"""Shared measuring code for the local real-car import (tools/local_cars_probe.py, local_cars_import.py)."""
import bpy, math
from mathutils import Vector, Matrix


def load(path):
    p = str(path)
    if p.lower().endswith(('.glb', '.gltf')):
        bpy.ops.import_scene.gltf(filepath=p)
    elif p.lower().endswith('.obj'):
        bpy.ops.wm.obj_import(filepath=p)
    else:
        raise RuntimeError('unsupported ' + p)
    return [o for o in bpy.context.scene.objects if o.type == 'MESH']


JUNK = ('icosphere', 'sphere', 'plane', 'ground', 'floor', 'backdrop', 'studio', 'shadow', 'environment', 'background', 'skydome', 'turntable')


def drop_junk(meshes):
    """Sketchfab scenes often carry a studio sphere / ground plane. Remove by name, then remove
    any object whose bounding box reaches far outside the core (1st-99th percentile) of the car."""
    keep = []
    for o in meshes:
        if any(j in o.name.lower() for j in JUNK):
            bpy.data.objects.remove(o, do_unlink=True)
        else:
            keep.append(o)
    pts = world_verts(keep)
    if len(pts) < 100:
        return keep
    # Flat objects lying at the very bottom (a name plate, a decal, a shadow card beside the car)
    # are not part of the car; they would stretch the bounding box and shrink the scale.
    zmin = min(p.z for p in pts)
    span = max(max(p.x for p in pts) - min(p.x for p in pts), max(p.y for p in pts) - min(p.y for p in pts))
    flat = []
    for o in keep:
        vs = [o.matrix_world @ Vector(c) for c in o.bound_box]
        dz = max(v.z for v in vs) - min(v.z for v in vs)
        if dz < 0.004 * span and min(v.z for v in vs) < zmin + 0.01 * span:
            flat.append(o)
    for o in flat:
        print('DROP flat', o.name, flush=True)
        keep.remove(o)
        bpy.data.objects.remove(o, do_unlink=True)
    pts = world_verts(keep)
    def pct(axis, q):
        v = sorted(p[axis] for p in pts[::max(1, len(pts) // 200000)])
        return v[int(q * (len(v) - 1))]
    lo = [pct(a, .01) for a in range(3)]
    hi = [pct(a, .99) for a in range(3)]
    size = [hi[a] - lo[a] for a in range(3)]
    out = []
    for o in keep:
        vs = [o.matrix_world @ Vector(c) for c in o.bound_box]
        bad = any(min(v[a] for v in vs) < lo[a] - .5 * size[a] or max(v[a] for v in vs) > hi[a] + .5 * size[a] for a in range(3))
        if bad:
            print('DROP', o.name, flush=True)
            bpy.data.objects.remove(o, do_unlink=True)
        else:
            out.append(o)
    return out


def world_verts(meshes):
    pts = []
    for o in meshes:
        m = o.matrix_world
        pts.extend(m @ v.co for v in o.data.vertices)
    return pts


def normalise(meshes, length_m, yaw_deg):
    """Bake a root transform so the car is nose +Y (after yaw), real length, ground at z = 0."""
    pts = world_verts(meshes)
    lo = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
    hi = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
    size = hi - lo
    centre = (lo + hi) / 2
    # Long horizontal axis onto Y.
    base_rot = Matrix.Rotation(math.pi / 2, 4, 'Z') if size.x > size.y else Matrix.Identity(4)
    long_len = max(size.x, size.y)
    s = length_m / long_len
    M = Matrix.Rotation(math.radians(yaw_deg), 4, 'Z') @ base_rot @ Matrix.Scale(s, 4) @ Matrix.Translation(-Vector((centre.x, centre.y, lo.z)))
    for o in [o for o in bpy.context.scene.objects if o.parent is None]:
        o.matrix_world = M @ o.matrix_world
    bpy.context.view_layer.update()
    return s


def estimate_wheels(meshes):
    """Wheel centres and radii. Preferred: a separate round object touching the ground in each
    quadrant (the tyre) - radius = half its height. Fallback: tyre chord just above the ground,
    restricted to the outer 0.3 m where splitters and underbody can't reach."""
    pts = world_verts(meshes)
    W = max(p.x for p in pts) - min(p.x for p in pts)
    L = max(p.y for p in pts) - min(p.y for p in pts)
    boxes = []
    for o in meshes:
        vs = [o.matrix_world @ Vector(c) for c in o.bound_box]
        lo = Vector((min(v.x for v in vs), min(v.y for v in vs), min(v.z for v in vs)))
        hi = Vector((max(v.x for v in vs), max(v.y for v in vs), max(v.z for v in vs)))
        boxes.append((lo, hi))
    wheels = {}
    for sx, sy, name in [(-1, 1, 'FL'), (1, 1, 'FR'), (-1, -1, 'RL'), (1, -1, 'RR')]:
        best = None
        for lo, hi in boxes:
            c = (lo + hi) / 2
            dy, dz = hi.y - lo.y, hi.z - lo.z
            if c.x * sx > 0.25 * W / 2 and c.y * sy > 0.15 * L / 2 and lo.z < 0.05 and 0.40 < dz < 1.0 and abs(dy - dz) < 0.12 * dz and 0.10 < hi.x - lo.x < 0.6:
                if best is None or dz > best[1].z - best[0].z:
                    best = (lo, hi)
        if best:
            lo, hi = best
            r = (hi.z - lo.z) / 2
            wheels[name] = {'y': round((lo.y + hi.y) / 2, 4), 'z': round(r, 4), 'r': round(r, 4),
                            'x_outer': round(max(abs(lo.x), abs(hi.x)), 4), 'x_center': round(abs(lo.x + hi.x) / 2, 4),
                            'width': round(hi.x - lo.x, 4), 'method': 'object'}
            continue
        q = [p for p in pts if p.x * sx > 0.25 * W / 2 and p.y * sy > 0.15 * L / 2]
        low = [p for p in q if p.z < 0.03]
        if len(low) < 3:
            wheels[name] = None
            continue
        ys = sorted(p.y for p in low)
        yc = ys[len(ys) // 2]
        # Outer tyre face from the lowest 12 cm only: arches, flares and sills sit higher.
        near = [p for p in q if abs(p.y - yc) < 0.25 and p.z < 0.12]
        xo = max(abs(p.x) for p in near)
        h = 0.05
        band = [p for p in q if p.z < h and abs(p.y - yc) < 0.55 and abs(p.x) > xo - 0.30]
        c = (max(p.y for p in band) - min(p.y for p in band)) / 2
        r = min(max((c * c + h * h) / (2 * h), 0.22), 0.48)
        # Vertical slice through the wheel centre: the tyre is solid from the ground up to 2r,
        # then there is air before the arch. The first gap gives the tyre top.
        zs = sorted(p.z for p in q if abs(p.y - yc) < 0.04 and xo - 0.25 < abs(p.x) <= xo + 0.01 and p.z < 1.0)
        top = None
        for a, b in zip(zs, zs[1:]):
            if a > 0.40 and b - a > 0.025:
                top = a
                break
        if top and 0.44 <= top <= 0.96:
            r = top / 2
        wheels[name] = {'y': round(yc, 4), 'z': round(r, 4), 'r': round(r, 4), 'x_outer': round(xo, 4), 'x_center': round(xo - 0.12, 4), 'width': 0.24, 'method': 'chord'}
    return wheels, {'W': round(W, 4), 'L': round(L, 4), 'H': round(max(p.z for p in pts), 4)}

def split_ground_objects(meshes):
    """Models that merge tyres into the body (or all four wheels into one mesh) are split into
    loose parts so each tyre becomes its own round object. Only big objects touching the
    ground are split; everything is re-joined into BODY / WHEEL_* later."""
    out = []
    for o in meshes:
        vs = [o.matrix_world @ Vector(c) for c in o.bound_box]
        lo_z = min(v.z for v in vs)
        dy = max(v.y for v in vs) - min(v.y for v in vs)
        crosses = min(v.x for v in vs) < -0.2 and max(v.x for v in vs) > 0.2
        if lo_z < 0.05 and (dy > 1.2 or crosses) and len(o.data.polygons) > 50:
            bpy.ops.object.select_all(action='DESELECT')
            o.select_set(True)
            bpy.context.view_layer.objects.active = o
            bpy.ops.object.mode_set(mode='EDIT')
            bpy.ops.mesh.separate(type='LOOSE')
            bpy.ops.object.mode_set(mode='OBJECT')
            out.extend(bpy.context.selected_objects)
        else:
            out.append(o)
    return out