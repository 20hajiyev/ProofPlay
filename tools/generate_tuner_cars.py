"""Detailed cel-shaded street cars, v2 (D-053). Original designs with real-car proportions and
real-car detail; no real model, badge or livery copied. Style: Auto Modellista / Highway
Warriors look pushed to current-gen detail - smooth subdivided bodies, crisp character lines the
ink pass (engine patch P-003) draws, multi-chamber lamps, grilles, intakes, handles, wipers, a
visible interior behind tinted glass.

Run: blender --background --factory-startup --python tools/generate_tuner_cars.py [-- D02 ...]

Body: a lofted cage of key stations along Y (nose +Y, left -X, D-002). Each station is a half
cross-section (floor, sill, door bulge, shoulder, beltline, greenhouse, roof edge, crown)
mirrored in X. Character lines (sill, beltline, zone boundaries) are creased, then the cage is
subdivided (Catmull-Clark) into a smooth shell; faces keep the material of their zone. Wheel
arches are cut with an exact boolean and get flared lips. Every detail is projected onto the
final surface with a BVH ray cast, so lamps, lines and decals sit exactly on the body.

Customisation (plan 2.13, owner: "modifikasiya olacaq"): optional parts are separate objects
named PART_<slot>_<variant>, parented to BODY (so they follow the body animation); rims are
RIM_<variant>_<wheel> under each wheel pivot. content/customization/<id>.json lists the slots,
variants and the stock choice; the game removes every variant that is not selected.

Output: assets/vehicles_toon/<id>/<id>.glb (+ _review.png), content/customization/<id>.json.
Physics dims (wheelbase, track, wheel size) match content/vehicles/<id>.json.
"""
import bpy, bmesh, math, sys, json
from pathlib import Path
from mathutils import Vector, Matrix
from mathutils.bvhtree import BVHTree

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import car_cabin  # noqa: E402  (cabin v3 and the driver, D-063)
import car_designs  # noqa: E402  (the data-driven roster, D-089)
OUT = ROOT / 'assets' / 'vehicles_toon'
CUSTOM = ROOT / 'content' / 'customization'


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def mat(name, rgb, rough=0.5, metal=0.0, emit=0.0, alpha=1.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    # Single-sided (glTF doubleSided false): from the cockpit the inside of the body shell must not
    # show the paint (D-060); the cabin gets its own lining.
    m.use_backface_culling = True
    p = m.node_tree.nodes['Principled BSDF']
    p.inputs['Base Color'].default_value = (*rgb, 1)
    p.inputs['Roughness'].default_value = rough
    p.inputs['Metallic'].default_value = metal
    if emit:
        p.inputs['Emission Color'].default_value = (*rgb, 1)
        p.inputs['Emission Strength'].default_value = emit
    if alpha < 1.0:
        p.inputs['Alpha'].default_value = alpha
        m.surface_render_method = 'BLENDED'  # glTF alphaMode BLEND -> transparent in the game
    return m


def link(name, verts, faces, mats, face_mats=None):
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], faces)
    me.update()
    o = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(o)
    for m in mats:
        o.data.materials.append(m)
    if face_mats:
        for poly, k in zip(o.data.polygons, face_mats):
            poly.material_index = k
    return o


def apply_mod(o, m):
    bpy.context.view_layer.objects.active = o
    bpy.ops.object.modifier_apply(modifier=m.name)


def box(name, loc, size, m, bev=0.0, rot=None, seg=1):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc)
    o = bpy.context.object
    o.name = name
    o.scale = size
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    o.data.materials.append(m)
    if bev:
        b = o.modifiers.new('bevel', 'BEVEL')
        b.width = bev
        b.segments = seg
        apply_mod(o, b)
    if rot:
        o.rotation_euler = rot
    return o


def cyl(name, loc, r, depth, m, rot=(0, math.pi / 2, 0), verts=24):
    bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=r, depth=depth, location=loc, rotation=rot)
    o = bpy.context.object
    o.name = name
    o.data.materials.append(m)
    return o


def torus(name, loc, major, minor, m, rot=(0, math.pi / 2, 0), seg=32, mseg=8):
    bpy.ops.mesh.primitive_torus_add(location=loc, rotation=rot, major_radius=major, minor_radius=minor,
                                     major_segments=seg, minor_segments=mseg)
    o = bpy.context.object
    o.name = name
    o.data.materials.append(m)
    return o


def tube_along(name, pts, r, m, sides=8):
    """A round tube through a polyline (arch lips, rails, cage bars)."""
    verts, faces = [], []
    for i, p in enumerate(pts):
        t = (pts[min(i + 1, len(pts) - 1)] - pts[max(i - 1, 0)]).normalized()
        a = t.orthogonal().normalized()
        b = t.cross(a).normalized()
        for k in range(sides):
            ang = 2 * math.pi * k / sides
            verts.append(tuple(p + (a * math.cos(ang) + b * math.sin(ang)) * r))
    for i in range(len(pts) - 1):
        for k in range(sides):
            a0, a1 = i * sides + k, i * sides + (k + 1) % sides
            faces.append((a0, a1, a1 + sides, a0 + sides))
    o = link(name, verts, faces, [m])
    fix_normals(o)
    return o


def fix_normals(o):
    bm = bmesh.new()
    bm.from_mesh(o.data)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(o.data)
    bm.free()


def join(objs, name):
    bpy.ops.object.select_all(action='DESELECT')
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.object.join()
    o = bpy.context.object
    o.name = name
    return o


# ---------------------------------------------------------------- body

class Body:
    """Key stations: (y, w, zs, zb, zt, wt, zone_to_next).
    w  half width at the door bulge   zs floor height         zb beltline height
    zt crown height on the centreline wt half width of the roof/deck edge"""

    ZONE_TOP = {'bonnet': 'paint', 'ws': 'glass', 'roof': 'paint', 'rw': 'glass', 'deck': 'paint', 'bed': 'trim'}
    CABIN = ('ws', 'roof', 'rw')
    NP = 11  # points on the half profile

    def __init__(self, stations):
        self.st = stations
        self.bvh = None
        self.obj = None

    @staticmethod
    def profile(w, zs, zb, zt, wt):
        g = w - 0.03 + (wt - (w - 0.03)) * 0.45  # greenhouse mid: tumblehome
        return [(0.0, zs), (w - 0.12, zs), (w - 0.045, zs + 0.05), (w - 0.012, zs + 0.15),
                (w, zs + (zb - zs) * 0.55), (w - 0.006, zb - 0.07), (w - 0.03, zb),
                (g, zb + (zt - zb) * 0.5), (wt, zt - 0.045), (wt * 0.55, zt - 0.01), (0.0, zt)]

    def params(self, y):
        st = self.st
        if y >= st[0][0]:
            return st[0][1:6]
        for a, b in zip(st, st[1:]):
            if b[0] <= y <= a[0]:
                t = (a[0] - y) / (a[0] - b[0])
                return tuple(a[k] + (b[k] - a[k]) * t for k in range(1, 6))
        return st[-1][1:6]

    def cage_point(self, y, t):
        p = self.profile(*self.params(y))
        i = min(int(t), self.NP - 2)
        f = t - i
        return (p[i][0] + (p[i + 1][0] - p[i][0]) * f, p[i][1] + (p[i + 1][1] - p[i][1]) * f)

    def normal2d(self, y, t):
        a = self.cage_point(y, max(0.0, t - 0.05))
        b = self.cage_point(y, min(self.NP - 1.0, t + 0.05))
        n = Vector((b[1] - a[1], -(b[0] - a[0])))
        if n.length < 1e-6:
            return Vector((0, 1))
        return n.normalized()

    def surf(self, y, t, off=0.0, side=1):
        """Point on the FINAL subdivided surface at station y, profile parameter t, lifted by off."""
        x, z = self.cage_point(y, t)
        n = self.normal2d(y, t)
        n3 = Vector((side * n.x, 0, n.y))
        p = Vector((side * x, y, z))
        if self.bvh:
            hit = self.bvh.ray_cast(p + n3 * 0.3, -n3, 0.6)
            if hit[0] is not None:
                return hit[0] + hit[1] * off
        return p + n3 * off

    def side_x(self, y, z, side):
        """Outer body surface x at (y, z) by a horizontal ray - for arch lips and handles."""
        hit = self.bvh.ray_cast(Vector((side * 3.0, y, z)), Vector((-side, 0, 0)), 3.0)
        return hit[0].x if hit[0] is not None else None

    def build(self, M):
        keys = ['paint', 'glass', 'trim', 'dark']
        ring = []
        for (y, w, zs, zb, zt, wt, zone) in self.st:
            p = self.profile(w, zs, zb, zt, wt)
            full = [(x, z) for x, z in p] + [(-x, z) for x, z in reversed(p[1:-1])]
            ring.append([(x, y, z) for x, z in full])
        n = len(ring[0])
        verts = [v for r in ring for v in r]
        faces, fm = [], []

        def half(j):
            return j if j < self.NP else 2 * (self.NP - 1) - j

        def seg_mat(j, zone):
            h = min(half(j), half((j + 1) % n))  # lower point index of the segment on the half profile
            if h == 0:
                return 'dark'
            if h in (6, 7):
                return 'glass' if zone in self.CABIN else 'paint'
            if h >= 8:
                return self.ZONE_TOP[zone]
            return 'paint'

        for i in range(len(ring) - 1):
            zone = self.st[i][6]
            for j in range(n):
                a, b = i * n + j, i * n + (j + 1) % n
                c, d = (i + 1) * n + (j + 1) % n, (i + 1) * n + j
                faces.append((a, d, c, b))
                fm.append(keys.index(seg_mat(j, zone)))
        # Nose and tail as a flat panel (D-073, tested live through Blender MCP): an inset ring at the
        # end station, then a small fan. A plain fan to the centre subdivided into a dome - the "bar
        # of soap" tail the owner called toy-like.
        for i, rev in ((0, True), (len(ring) - 1, False)):
            cx = sum(v[0] for v in ring[i]) / n
            cz = sum(v[2] for v in ring[i]) / n
            y = ring[i][0][1]
            base = len(verts)
            for (x, _, z) in ring[i]:
                verts.append((cx + (x - cx) * 0.82, y, cz + (z - cz) * 0.82))
            for j in range(n):
                q = (i * n + j, i * n + (j + 1) % n, base + (j + 1) % n, base + j)
                faces.append(q[::-1] if rev else q)
                fm.append(0)
            verts.append((cx, y, cz))
            c = len(verts) - 1
            for j in range(n):
                f = (base + j, base + (j + 1) % n, c)
                faces.append(f[::-1] if rev else f)
                fm.append(0)
        o = link('body_shell', verts, faces, [M[k] for k in keys], fm)
        # Crease the character lines: sill (3), beltline (6), roof edge (8, half), zone boundaries.
        bm = bmesh.new()
        bm.from_mesh(o.data)
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        cl = bm.edges.layers.float.get('crease_edge') or bm.edges.layers.float.new('crease_edge')
        bm.verts.ensure_lookup_table()
        nring = len(ring)
        zone_change = {i for i in range(1, nring - 1) if self.st[i][6] != self.st[i - 1][6]}
        for e in bm.edges:
            a, b = e.verts[0].index, e.verts[1].index
            if a >= nring * n or b >= nring * n:
                continue
            ia, ja = divmod(a, n)
            ib, jb = divmod(b, n)
            if ja == jb and abs(ia - ib) == 1:  # longitudinal edge along profile point ja
                h = half(ja)
                if h in (3, 6):
                    e[cl] = 1.0
                elif h == 8:
                    e[cl] = 0.9   # roof edge: chiselled, not pillowy (was 0.7)
                elif h == 1:
                    e[cl] = 0.6   # the underside edge reads in profile
            elif ia == ib and ia in zone_change:  # ring edge where the zone changes
                if min(half(ja), half(jb)) >= 6:
                    e[cl] = 1.0
            elif ia == ib and ia in (0, nring - 1):  # nose and tail face outline: a crisp edge
                e[cl] = 1.0
            elif ia == ib and ia in (1, nring - 2):  # the corners where the sides turn into nose / tail
                e[cl] = 0.7
        bm.to_mesh(o.data)
        bm.free()
        sub = o.modifiers.new('subd', 'SUBSURF')
        sub.levels = 2
        sub.render_levels = 2
        apply_mod(o, sub)
        self.obj = o
        return o

    def cut_arches(self, axles, r, width, M):
        o = self.obj
        self.arches = [(y, r, r + 0.08) for y in axles]  # (y, centre z, radius) of each opening
        for y in axles:
            c = cyl('arch_cut', (0, y, r), r + 0.08, 2 * (width + 1.0), M['well'], verts=48)
            mod = o.modifiers.new('arch', 'BOOLEAN')
            mod.operation = 'DIFFERENCE'
            mod.solver = 'EXACT'
            mod.object = c
            mod.material_mode = 'TRANSFER'
            apply_mod(o, mod)
            bpy.data.objects.remove(c)

    def cabin_lining(self, y0, y1, z_from, M):
        """Headliner and pillar trims: the shell faces above the beltline over the cabin, pulled 2 cm
        inside and turned to face in, in the interior material (seen from the cockpit camera)."""
        src = self.obj
        bm = bmesh.new()
        bm.from_mesh(src.data)
        # paint/trim only (slots 0 and 2): the windows stay see-through
        keep = [f for f in bm.faces if f.material_index in (0, 2) and y1 <= f.calc_center_median().y <= y0 and f.calc_center_median().z > z_from]
        drop = [f for f in bm.faces if f not in set(keep)]
        bmesh.ops.delete(bm, geom=drop, context='FACES')
        bm.normal_update()
        for v in bm.verts:
            v.co -= v.normal * 0.02
        for f in bm.faces:
            f.normal_flip()
            f.material_index = 0
        me = bpy.data.meshes.new('cabin_lining')
        bm.to_mesh(me)
        bm.free()
        o = bpy.data.objects.new('cabin_lining', me)
        bpy.context.scene.collection.objects.link(o)
        o.data.materials.clear()
        o.data.materials.append(M['headliner_l'])  # light fabric: the cockpit view is not a dark cave
        return o

    def pillars(self, car, M):
        """A- and C-pillars (D-068). The side glass and the windscreen / rear glass meet at profile t=8
        with no pillar between them, so from the cockpit the roof floated over an open gap (owner:
        "bugs on the body in first person"). A paint band on the outside, a lining band facing in."""
        out = []
        for name, y0, y1, w in (('a_pillar', car.y_ws0, car.y_roof0, 0.085), ('c_pillar', car.y_rw0, car.y_rw1, 0.15)):
            dt = w / 0.3
            out.append(self.patch(name, y0, y1, 8.0 - dt / 2, 8.0 + dt / 2, M['paint'], off=0.005, ny=16, nt=2))
            inner = self.patch(name + '_trim', y0, y1, 8.0 - dt / 2, 8.0 + dt / 2, M['headliner_l'], off=-0.022, ny=16, nt=2)
            inner.data.flip_normals()
            out.append(inner)
        return out

    def finish_surface(self):
        o = self.obj
        for p in o.data.polygons:
            p.use_smooth = True
        o.data.set_sharp_from_angle(angle=math.radians(35))
        self.bvh = BVHTree.FromObject(o, bpy.context.evaluated_depsgraph_get())

    # -------- details on the surface
    def patch(self, name, y0, y1, t0, t1, m, off=0.006, mirror=True, ny=6, nt=4):
        arches = getattr(self, 'arches', [])
        if arches and abs(y1 - y0) > 0.3:  # fine steps along a long strip so the cut at an arch is clean
            ny = max(ny, int(abs(y1 - y0) / 0.03))
        verts, faces = [], []
        for side in ((1, -1) if mirror else (1,)):
            base = len(verts)
            for iy in range(ny + 1):
                y = y0 + (y1 - y0) * iy / ny
                for it in range(nt + 1):
                    t = t0 + (t1 - t0) * it / nt
                    verts.append(tuple(self.surf(y, t, off, side)))
            for iy in range(ny):
                for it in range(nt):
                    a = base + iy * (nt + 1) + it
                    faces.append((a, a + 1, a + nt + 2, a + nt + 1))
        if arches:  # drop faces over a wheel opening: the surface there was cut away (D-075)
            def over_arch(f):
                cy = sum(verts[i][1] for i in f) / len(f)
                cz = sum(verts[i][2] for i in f) / len(f)
                return any((cy - ay) ** 2 + (cz - az) ** 2 < (ar + 0.012) ** 2 for ay, az, ar in arches)
            faces = [f for f in faces if not over_arch(f)]
        o = link(name, verts, faces, [m])
        bm = bmesh.new()
        bm.from_mesh(o.data)
        for v in [v for v in bm.verts if not v.link_faces]:
            bm.verts.remove(v)
        for f in bm.faces:  # face outwards: away from the car's long axis
            c = f.calc_center_median()
            if f.normal.dot(Vector((c.x, 0, c.z - 0.55))) < 0:
                f.normal_flip()
        bm.to_mesh(o.data)
        bm.free()
        return o

    def line_across(self, name, y, t0, t1, m, width=0.012):
        return self.patch(name, y - width / 2, y + width / 2, t0, t1, m, off=0.004, ny=1, nt=10)

    def line_along(self, name, y0, y1, t, m, width=0.012):
        dt = width / 0.3
        return self.patch(name, y0, y1, t - dt / 2, t + dt / 2, m, off=0.004, ny=16, nt=1)


# ---------------------------------------------------------------- wheels and rims

def ring_mesh(name, c, r0, r1, x, m, n=40):
    verts, faces = [], []
    for k in range(n):
        a = 2 * math.pi * k / n
        for r in (r0, r1):
            verts.append((c.x + x, c.y + math.sin(a) * r, c.z + math.cos(a) * r))
    for k in range(n):
        a, b = 2 * k, 2 * ((k + 1) % n)
        faces.append((a, a + 1, b + 1, b) if x > 0 else (a, b, b + 1, a + 1))
    return link(name, verts, faces, [m])


def band(name, c, r, x0, x1, m, n=40, inward=False):
    verts, faces = [], []
    for k in range(n):
        a = 2 * math.pi * k / n
        for x in (x0, x1):
            verts.append((c.x + x, c.y + math.sin(a) * r, c.z + math.cos(a) * r))
    for k in range(n):
        a, b = 2 * k, 2 * ((k + 1) % n)
        faces.append((a, b, b + 1, a + 1) if not inward else (a, a + 1, b + 1, b))
    o = link(name, verts, faces, [m])
    bm = bmesh.new()
    bm.from_mesh(o.data)
    for f in bm.faces:
        q = f.calc_center_median() - c
        out = Vector((0, q.y, q.z)) * (-1 if inward else 1)
        if f.normal.dot(out) < 0:
            f.normal_flip()
    bm.to_mesh(o.data)
    bm.free()
    return o


def bake(parts, name, c):
    bpy.ops.object.select_all(action='DESELECT')
    for p in parts:
        p.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    o = join(parts, name)
    o.data.transform(Matrix.Translation(-c))
    o.location = (0, 0, 0)
    return o


def tyre(tag, c, r, width, M, side):
    """Open tyre (the rim face must show): rounded shoulders, sidewall, tread grooves, brake disc."""
    rim_r = r * 0.7
    hw = width / 2
    parts = [band('tread', c, r, -hw + 0.025, hw - 0.025, M['tyre'], 48)]
    for s in (-1, 1):
        # shoulder: a short cone from the tread edge to the sidewall
        parts.append(band('shoulder', c, r - 0.012, s * (hw - 0.025), s * hw, M['tyre'], 48))
        parts.append(ring_mesh('sidewall', c, rim_r, r - 0.012, s * hw, M['tyre'], 48))
        parts.append(ring_mesh('sidewall_bead', c, rim_r * 1.02, rim_r * 1.12, s * (hw + 0.004), M['tyre_side'], 40))
    # raised sidewall lettering: two arcs of short blocks read as a tyre brand at race distance
    for k in range(36):
        a = 2 * math.pi * k / 48 + (0.0 if k < 18 else math.pi - 2 * math.pi * 18 / 48)
        if k % 6 == 5:
            continue  # word gaps
        rr = (rim_r + r) / 2 + 0.01
        q = c + Vector((side * (hw + 0.003), math.sin(a) * rr, math.cos(a) * rr))
        blk = box('letter', q, (0.004, 0.016, 0.026), M['tyre_letter'])
        blk.rotation_euler = (-a, 0, 0)
        parts.append(blk)
    for gx in (-0.35, 0.0, 0.35):  # circumferential tread grooves
        parts.append(band('groove', c, r + 0.0015, gx * width - 0.006, gx * width + 0.006, M['groove'], 48))
    parts.append(band('barrel', c, rim_r * 0.98, -hw + 0.01, hw - 0.01, M['trim'], 32, inward=True))
    parts.append(cyl('disc', c + Vector((side * (hw - 0.09), 0, 0)), rim_r * 0.78, 0.028, M['disc'], verts=32))
    parts.append(cyl('disc_hat', c + Vector((side * (hw - 0.07), 0, 0)), rim_r * 0.34, 0.03, M['grey'], verts=20))
    return bake(parts, 'WHEEL_' + tag, c)


RIMS = ['five', 'mesh', 'dish', 'ten', 'fan', 'y_spoke', 'aero', 'split']


def rim(variant, tag, c, r, width, M, side):
    rim_r = r * 0.7
    face = side * (width / 2)            # outer face plane
    polished = variant in ('dish', 'split')  # a polished stepped lip
    parts = [ring_mesh('lip', c, rim_r * 0.9, rim_r, face + side * 0.004, M['rim_lip' if polished else 'rim'], 40),
             torus('lip_edge', c + Vector((face + side * 0.004, 0, 0)), rim_r * 0.99, 0.008, M['rim'], seg=40, mseg=6)]
    dish = {'dish': 0.09, 'split': 0.065}.get(variant, 0.035)
    parts.append(ring_mesh('dish_wall', c, rim_r * 0.88, rim_r * 0.9, face - side * dish, M['trim'], 40))
    parts.append(band('dish_band', c, rim_r * 0.9, face - side * dish, face + side * 0.004, M['rim_lip' if polished else 'rim'], 40, inward=True))
    hub_x = face - side * (dish * 0.4)
    parts.append(cyl('hub', c + Vector((hub_x, 0, 0)), rim_r * 0.22, 0.05, M['rim'], verts=20))
    parts.append(cyl('cap', c + Vector((hub_x + side * 0.026, 0, 0)), rim_r * 0.12, 0.012, M['trim'], verts=16))
    parts.append(cyl('valve', c + Vector((face - side * 0.01, 0, rim_r * 0.86)), 0.006, 0.03, M['trim'], verts=6))
    for k in range(5):  # lug nuts
        a = 2 * math.pi * k / 5
        q = c + Vector((hub_x + side * 0.026, math.sin(a) * rim_r * 0.16, math.cos(a) * rim_r * 0.16))
        parts.append(cyl('lug', q, 0.011, 0.02, M['grey'], verts=8))

    def spoke(a, wdt, dep, length=0.72, r0=0.2, twist=0.0):
        mid = (r0 + (r0 + length * 0.9)) / 2
        q = c + Vector((face - side * dep * 0.5, math.sin(a) * rim_r * mid, math.cos(a) * rim_r * mid))
        s = box('spoke', q, (dep, rim_r * wdt, rim_r * length * 0.9), M['rim'], 0.004)
        s.rotation_euler = (-a, twist, 0)
        parts.append(s)

    if variant == 'five':
        for k in range(5):
            spoke(2 * math.pi * k / 5, 0.2, 0.03)
    elif variant == 'ten':
        for k in range(10):
            spoke(2 * math.pi * k / 10, 0.09, 0.028)
    elif variant == 'dish':
        for k in range(6):
            spoke(2 * math.pi * k / 6, 0.1, 0.02, length=0.62)
    elif variant == 'mesh':  # cross-spoke mesh: two leaning sets
        for k in range(10):
            a = 2 * math.pi * k / 10
            spoke(a + 0.16, 0.05, 0.022, twist=0.0)
            spoke(a - 0.16, 0.05, 0.022, twist=0.0)
        parts.append(ring_mesh('mesh_ring', c, rim_r * 0.84, rim_r * 0.9, face - side * 0.02, M['rim'], 40))
    elif variant == 'fan':  # turbofan: a solid face with dark blade slots
        parts.append(cyl('fan_face', c + Vector((face - side * 0.02, 0, 0)), rim_r * 0.88, 0.012, M['rim'], verts=40))
        for k in range(14):
            a = 2 * math.pi * k / 14
            q = c + Vector((face - side * 0.012, math.sin(a) * rim_r * 0.58, math.cos(a) * rim_r * 0.58))
            s = box('fan_slot', q, (0.006, rim_r * 0.07, rim_r * 0.48), M['trim'])
            s.rotation_euler = (-a, 0, 0.35 * side)
            parts.append(s)
    elif variant == 'y_spoke':  # modern forged: six Y spokes, a stem forking near the barrel
        for k in range(6):
            a = 2 * math.pi * k / 6
            spoke(a, 0.15, 0.034, length=0.36, r0=0.2)
            spoke(a + 0.12, 0.075, 0.028, length=0.44, r0=0.5)
            spoke(a - 0.12, 0.075, 0.028, length=0.44, r0=0.5)
    elif variant == 'aero':  # EV-style aero cover: a flat face with five dark petal openings
        parts.append(cyl('aero_face', c + Vector((face - side * 0.018, 0, 0)), rim_r * 0.86, 0.01, M['rim'], verts=40))
        parts.append(torus('aero_ring', c + Vector((face - side * 0.012, 0, 0)), rim_r * 0.45, 0.009, M['trim'], seg=32, mseg=4))
        for k in range(5):
            a = 2 * math.pi * k / 5 + 0.3
            q = c + Vector((face - side * 0.011, math.sin(a) * rim_r * 0.68, math.cos(a) * rim_r * 0.68))
            o = box('aero_slot', q, (0.006, rim_r * 0.32, rim_r * 0.14), M['trim'])
            o.rotation_euler = (-a - math.pi / 2 + 0.35, 0, 0)
            parts.append(o)
    elif variant == 'split':  # multi-piece: seven split (twin) spokes on a deep polished lip
        for k in range(7):
            a = 2 * math.pi * k / 7
            spoke(a + 0.055, 0.055, 0.026)
            spoke(a - 0.055, 0.055, 0.026)
        for k in range(14):  # assembly bolts round the centre-to-lip joint
            a = 2 * math.pi * k / 14
            parts.append(cyl('split_bolt', c + Vector((face - side * 0.01, math.sin(a) * rim_r * 0.86, math.cos(a) * rim_r * 0.86)), 0.006, 0.01, M['chrome'], verts=6))
    return bake(parts, f'RIM_{variant}_{tag}', c)


def wheel_set(M, wb, track, r, width):
    for tag, (sx, sy) in {'FL': (-1, 1), 'FR': (1, 1), 'RL': (-1, -1), 'RR': (1, -1)}.items():
        c = Vector((sx * track / 2, sy * wb / 2, r))
        pivot = bpy.data.objects.new('WHEEL_PIVOT_' + tag, None)
        bpy.context.scene.collection.objects.link(pivot)
        pivot.location = c
        side = sx
        t = tyre(tag, c, r, width, M, side)
        t.parent = pivot
        for v in RIMS:
            rm = rim(v, tag, c, r, width, M, side)
            rm.parent = pivot


# ---------------------------------------------------------------- shared car details

def palette(paint, accent, rim_rgb, caliper=(0.85, 0.1, 0.08), suit=(0.12, 0.2, 0.55)):
    return {
        'paint': mat('toon_paint', paint, 0.12),  # a crisp toon glint on the panels (D-068)
        'accent': mat('toon_accent', accent, 0.4),
        'trim': mat('toon_trim', (0.04, 0.04, 0.05), 0.6),
        'dark': mat('toon_under', (0.08, 0.08, 0.09), 0.8),
        'line': mat('toon_line', (0.015, 0.015, 0.02), 0.9),
        'well': mat('toon_well', (0.03, 0.03, 0.035), 0.9),
        'glass': mat('toon_glass', (0.05, 0.07, 0.1), 0.05, alpha=0.8),
        'lens': mat('toon_lens', (0.85, 0.9, 0.95), 0.05, alpha=0.15),
        'grey': mat('toon_grey', (0.45, 0.46, 0.48), 0.45, 0.15),
        'chrome': mat('toon_chrome', (0.88, 0.89, 0.92), 0.15, 0.3),
        'carbon': mat('toon_carbon', (0.11, 0.11, 0.12), 0.35),
        'rim': mat('toon_rim', rim_rgb, 0.3, 0.12),
        'rim_lip': mat('toon_rim_lip', (0.9, 0.9, 0.92), 0.12, 0.3),
        'disc': mat('toon_disc', (0.36, 0.36, 0.38), 0.5, 0.2),
        'tyre': mat('toon_tyre', (0.06, 0.06, 0.07), 0.9),
        'tyre_side': mat('toon_tyre_side', (0.12, 0.12, 0.13), 0.85),
        'tyre_letter': mat('toon_tyre_letter', (0.55, 0.55, 0.57), 0.8),
        'spring': mat('toon_spring', (0.85, 0.2, 0.1), 0.5),
        'groove': mat('toon_groove', (0.02, 0.02, 0.02), 1.0),
        'lamp': mat('toon_lamp', (1.0, 0.97, 0.9), 0.2, 0, 3.0),
        'reflector': mat('toon_reflector', (0.9, 0.92, 0.95), 0.1, 0.3),
        'tail': mat('toon_tail', (0.95, 0.04, 0.03), 0.2, 0, 3.0),
        'tail_dim': mat('toon_tail_dim', (0.45, 0.03, 0.03), 0.3),
        'amber': mat('toon_amber', (1.0, 0.5, 0.05), 0.2, 0, 1.5),
        'drl': mat('toon_drl_NoInk', (0.92, 0.96, 1.0), 0.2, 0, 2.2),  # always-on LED day lights (D-080)
        'mirror_l': mat('mirror_view_L_NoInk', (0.75, 0.8, 0.85), 0.1),
        'mirror_r': mat('mirror_view_R_NoInk', (0.75, 0.8, 0.85), 0.1),
        'amber_l': mat('toon_ind_left', (1.0, 0.5, 0.05), 0.2, 0, 1.5),
        'amber_r': mat('toon_ind_right', (1.0, 0.5, 0.05), 0.2, 0, 1.5),
        'dial': mat('toon_dial', (0.95, 0.95, 0.92), 0.4),
        'needle': mat('toon_needle', (1.0, 0.25, 0.1), 0.3, 0, 1.2),
        'reverse': mat('toon_reverse', (0.95, 0.95, 0.95), 0.2),
        'plate': mat('toon_plate', (0.93, 0.93, 0.88), 0.6),
        'interior': mat('toon_interior', (0.13, 0.13, 0.14), 0.8),
        'headliner_l': mat('toon_headliner_lining', (0.55, 0.54, 0.52), 0.9),
        'seat': mat('toon_seat', (0.18, 0.18, 0.2), 0.9),
        'seat_accent': mat('toon_seat_accent', (0.6, 0.1, 0.08), 0.9),
        'caliper': mat('toon_caliper', caliper, 0.4),
        'suit': mat('toon_suit', suit, 0.8),
        'skin': mat('toon_skin', (0.78, 0.58, 0.46), 0.8),
        'harness': mat('toon_harness', (0.08, 0.08, 0.1), 0.8),
        'screen': mat('toon_screen', (0.2, 0.55, 0.9), 0.2, 0, 1.2),
        'helmet': mat('toon_helmet', (0.93, 0.93, 0.95), 0.25),
        'visor': mat('toon_visor', (0.03, 0.04, 0.06), 0.05),
        'glove': mat('toon_glove', (0.06, 0.06, 0.07), 0.8),
    }


class Car:
    def __init__(self, cid, b, M, dims):
        self.id, self.b, self.M = cid, b, M
        self.wb, self.track, self.r, self.width = dims
        self.fixed = []            # joined into BODY
        self.special = {}          # name -> (objects, pivot location, pivot rotation): animated by the game
        self.parts = {}            # slot -> {variant: [objects]}
        self.stock = {}
        st = b.st
        self.front, self.rear = st[0][0], st[-1][0]
        self.y_ws0 = next(s[0] for s in st if s[6] == 'ws')
        self.y_roof0 = next(s[0] for s in st if s[6] == 'roof')
        self.y_rw0 = next(s[0] for s in st if s[6] == 'rw')
        self.y_rw1 = st[[s[0] for s in st].index(self.y_rw0) + 1][0]

    def fy(self, x, z, front=True):
        """Nose (front) or tail surface y at (x, z): subdivision rounds the caps, so face-mounted
        parts are placed on the real surface, not the cage."""
        d = -1 if front else 1
        o = Vector((x, (self.front + 1.0) if front else (self.rear - 1.0), z))
        hit = self.b.bvh.ray_cast(o, Vector((0, d, 0)), 2.5)
        y = hit[0].y if hit[0] is not None else (self.front if front else self.rear)
        band = getattr(self, 'bumper_band', {}).get(front)
        if band and band[0] - 0.005 <= z <= band[1] + 0.005:
            y -= d * band[2]  # on the bumper's face (D-074)
        return y

    def top_z(self, x, y):
        hit = self.b.bvh.ray_cast(Vector((x, y, 5.0)), Vector((0, 0, -1)), 6.0)
        return hit[0].z if hit[0] is not None else 1.0

    def add(self, *objs):
        self.fixed.extend(objs)

    def animated(self, name, objs, loc, rot=(0, 0, 0)):
        """A group the game moves on its own (D-060): parented to an empty `name` at `loc` whose local
        Z (Blender) - the game's local Y - is the axis it turns about."""
        self.special.setdefault(name, ([], Vector(loc), rot))[0].extend(objs)

    def variant(self, slot, name, objs, stock=False):
        self.parts.setdefault(slot, {})[name] = list(objs)
        if stock:
            self.stock[slot] = name

    # ---- generic real-car detail
    def glasshouse(self, b_pillars=()):
        b, M = self.b, self.M
        self.add(b.patch('a_pillar', self.y_ws0 - 0.03, self.y_roof0 + 0.02, 6.7, 7.95, M['trim'], ny=10, nt=3))
        self.add(b.patch('c_pillar', self.y_rw0 - 0.02, self.y_rw1 + 0.03, 7.35, 7.95, M['paint'], ny=10, nt=3))
        self.add(b.line_along('belt_seal', self.y_ws0 - 0.03, self.y_rw1 + 0.03, 6.05, M['trim'], 0.028))
        self.add(b.line_along('roof_seal', self.y_roof0, self.y_rw0, 7.95, M['trim'], 0.024))
        for yb in b_pillars:
            self.add(b.patch('b_pillar', yb - 0.05, yb + 0.05, 6.0, 7.9, M['trim'], ny=1, nt=6))
        # wipers resting at the windscreen base
        for tag, sx, ln in (('WIPER_L', -0.05, 0.62), ('WIPER_R', 0.52, 0.5)):
            p0 = b.surf(self.y_ws0 - 0.1, 8.6, 0.012, 1 if sx >= 0 else -1)
            p0 = Vector((sx, p0.y, p0.z))
            n = b.normal2d(self.y_ws0 - 0.1, 8.6)
            tilt = -math.atan2(0.6, 0.8) if n.length < 1e-6 else -math.atan2(0.62, 0.78)
            self.animated(tag, [box('wiper', p0 + Vector((-ln / 2, 0, 0)), (ln, 0.014, 0.012), M['trim'])], p0, (tilt, 0, 0))
        # roof antenna
        p = b.surf(self.y_rw0 + 0.12, 10.0, 0.0, 1)
        self.add(cyl('antenna', p + Vector((0.2, 0, 0.18)), 0.004, 0.38, M['trim'], rot=(-0.35, 0, 0), verts=6))
        self.add(cyl('antenna_base', p + Vector((0.2, 0, 0.01)), 0.018, 0.03, M['trim'], rot=(0, 0, 0), verts=10))

    def doors(self, cuts, handles):
        b, M = self.b, self.M
        for y in cuts:
            self.add(b.line_across('door_cut', y, 2.1, 6.0, M['line']))
        self.add(b.line_along('door_bottom', max(cuts), min(cuts), 2.35, M['line']))
        for y in handles:
            for s in (-1, 1):
                p = b.surf(y, 5.45, 0.012, s)
                self.add(box('handle', p, (0.02, 0.13, 0.03), M['trim'], 0.006))
                self.add(box('handle_recess', p - Vector((s * 0.006, 0, 0)), (0.008, 0.16, 0.05), M['dark']))

    def panel_lines(self, bonnet_y, boot_y=None, fuel_y=None):
        b, M = self.b, self.M
        self.add(b.line_across('bonnet_cut', bonnet_y, 6.1, 10.0, M['line']))
        self.add(b.line_along('bonnet_side', self.front - 0.14, bonnet_y, 6.25, M['line']))
        if boot_y is not None:
            self.add(b.line_across('boot_cut', boot_y, 6.1, 10.0, M['line']))
            self.add(b.line_along('boot_side', boot_y, self.rear + 0.1, 6.25, M['line']))
        if fuel_y is not None:
            p = b.surf(fuel_y, 5.1, 0.003, 1)
            self.add(torus('fuel_door', p, 0.075, 0.005, M['line'], rot=(0, math.pi / 2, 0), seg=24, mseg=4))
        # side repeaters on the front wings
        for s in (-1, 1):
            p = b.surf(self.front - 0.55, 5.0, 0.006, s)
            self.add(box('repeater', p, (0.012, 0.07, 0.025), M['amber_l' if s < 0 else 'amber_r'], 0.004))

    def mirrors(self):
        b, M = self.b, self.M
        for s in (-1, 1):
            p = b.surf(self.y_ws0 - 0.2, 6.3, 0.0, s)
            # big wing mirrors bolted to the door (owner: "bigger", "they look detached from the body"):
            # a sail mount plate sunk into the door skin, a thick tapered arm, housing, bezel, live glass
            self.add(box('mirror_mount', p + Vector((s * 0.0, 0.02, 0.04)), (0.05, 0.12, 0.08), M['trim'], 0.015, seg=2))
            self.add(box('mirror_arm', p + Vector((s * 0.07, 0.01, 0.05)), (0.16, 0.08, 0.05), M['trim'], 0.018, seg=2))
            self.add(box('mirror_arm_cap', p + Vector((s * 0.09, 0.012, 0.075)), (0.1, 0.07, 0.03), M['paint'], 0.012))
            self.add(box('mirror', p + Vector((s * 0.235, 0.014, 0.13)), (0.29, 0.1, 0.2), M['paint'], 0.035, seg=2))
            self.add(box('mirror_bezel', p + Vector((s * 0.235, -0.04, 0.13)), (0.286, 0.012, 0.196), M['trim'], 0.012))
            self.add(mirror_quad('MIRROR_L' if s < 0 else 'MIRROR_R', p + Vector((s * 0.235, -0.048, 0.13)), 0.264, 0.174, M['mirror_l' if s < 0 else 'mirror_r'], self))  # live views (D-064)

    def interior(self, rear_seats=True):
        """Cabin v3: tools/car_cabin.py (D-063)."""
        rgb = tuple(self.M['suit'].node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value[:3])
        car_cabin.materials(self.M, rgb, getattr(self, 'cabin_style', self.id))
        car_cabin.interior(self, rear_seats)

    def driver(self):
        """The driver is a separate character (tools/generate_driver.py, D-076): the car only marks
        the seat - DRIVER_ANCHOR at the hip, where the game sits the character - and the cockpit eye
        for review renders."""
        ys, zc, zh = self.seat
        anchor = bpy.data.objects.new('DRIVER_ANCHOR', None)
        bpy.context.scene.collection.objects.link(anchor)
        anchor.location = (-0.37, ys - 0.02, zc + 0.12)
        self.markers = getattr(self, 'markers', []) + [anchor]
        eye = bpy.data.objects.new('DRIVER_EYE', None)  # matches the character's eye (same seat rule)
        bpy.context.scene.collection.objects.link(eye)
        eye.location = (-0.37, ys - 0.21 + 0.0996, zh + 0.020)  # on_head((0, 1, 0.2), 0.004) of car_cabin.driver
        self.eye = eye

    def bone(self, name, objs, start, direction):
        """An arm bone: meshes authored along +Z from the origin, under an empty at `start` turned
        so +Z points along `direction` (the game's local +Y after the glTF axis swap)."""
        self.bones = getattr(self, 'bones', [])
        self.bones.append((name, objs, Vector(start), Vector(direction)))

    def plate(self, y, z, front):
        M = self.M
        d = 1 if front else -1
        y = min(self.fy(-0.25, z, True), self.fy(0.25, z, True)) if front else max(self.fy(-0.25, z, False), self.fy(0.25, z, False))
        self.add(box('plate_frame', (0, y + d * 0.006, z), (0.5, 0.012, 0.15), M['trim'], 0.008))
        self.add(box('plate', (0, y + d * 0.013, z), (0.46, 0.006, 0.11), M['plate']))

    def exhausts(self, layouts, stock):
        """Exhaust slot variants: {'single': [(x, r)], 'dual': [...]}; tips under the rear bumper."""
        M = self.M
        z = self.b.st[-1][2] + 0.07
        for name, tips in layouts.items():
            objs = []
            for x, r in tips:
                ry = self.fy(x, z + 0.08, False)
                objs.append(cyl('tip', (x, ry - 0.03, z), r, 0.16, M['chrome'], rot=(math.pi / 2, 0, 0), verts=20))
                objs.append(cyl('tip_in', (x, ry - 0.1, z), r * 0.8, 0.02, M['well'], rot=(math.pi / 2, 0, 0), verts=16))
                objs.append(cyl('pipe', (x, ry + 0.25, z + 0.02), r * 0.7, 0.5, M['grey'], rot=(math.pi / 2, 0, 0), verts=12))
            self.variant('exhaust', name, objs, stock=(name == stock))

    def spoilers(self, stock, deck_z, deck_y, width):
        """Spoiler slot: none / ducktail lip / street wing / GT wing on swan-neck mounts."""
        M = self.M
        deck_z = min(self.top_z(0, deck_y), self.top_z(width * 0.3, deck_y))  # sit on the real surface
        self.variant('spoiler', 'none', [], stock=(stock == 'none'))
        self.variant('spoiler', 'lip', [box('lip', (0, deck_y - 0.06, deck_z + 0.02), (width * 0.9, 0.16, 0.035), M['paint'], 0.012, rot=(0.25, 0, 0))],
                     stock=(stock == 'lip'))
        wing = [box('wing_blade', (0, deck_y - 0.05, deck_z + 0.24), (width, 0.26, 0.035), M['paint'], 0.012, rot=(0.08, 0, 0))]
        for s in (-1, 1):
            wing.append(box('wing_post', (s * width * 0.3, deck_y, deck_z + 0.11), (0.035, 0.12, 0.22), M['paint'], 0.01))
            wing.append(box('wing_end', (s * width * 0.5, deck_y - 0.05, deck_z + 0.25), (0.012, 0.3, 0.1), M['paint'], 0.004))
        self.variant('spoiler', 'wing', wing, stock=(stock == 'wing'))
        gt = [box('gt_blade', (0, deck_y - 0.12, deck_z + 0.36), (width * 1.05, 0.3, 0.03), M['carbon'], 0.01, rot=(0.14, 0, 0)),
              box('gt_flap', (0, deck_y - 0.26, deck_z + 0.39), (width * 1.05, 0.1, 0.02), M['carbon'], 0.006, rot=(0.5, 0, 0))]
        for s in (-1, 1):
            gt.append(box('gt_neck', (s * width * 0.28, deck_y - 0.02, deck_z + 0.2), (0.02, 0.1, 0.36), M['trim'], 0.005, rot=(-0.3, 0, 0)))
            gt.append(box('gt_end', (s * width * 0.53, deck_y - 0.14, deck_z + 0.37), (0.01, 0.4, 0.2), M['carbon'], 0.004))
        self.variant('spoiler', 'gt', gt, stock=(stock == 'gt'))

    def aero(self, stock_front='none', stock_side='none', stock_rear='none'):
        b, M = self.b, self.M
        st = b.st
        fw = st[0][1]
        zf = st[0][2]
        yf = self.fy(0, zf + 0.04) + 0.03
        yr = self.fy(0, st[-1][2] + 0.04, False) - 0.03
        self.variant('aero_f', 'none', [], stock=(stock_front == 'none'))
        self.variant('aero_f', 'lip', [box('front_lip', (0, yf - 0.05, zf - 0.02), (fw * 2.0, 0.12, 0.022), M['carbon'], 0.008)],
                     stock=(stock_front == 'lip'))
        splitter = [box('splitter', (0, yf - 0.08, zf - 0.03), (fw * 2.1, 0.2, 0.02), M['carbon'], 0.006)]
        for s in (-1, 1):
            splitter.append(box('canard', (s * fw * 0.95, self.fy(s * fw * 0.95, zf + 0.12) - 0.02, zf + 0.12), (0.14, 0.18, 0.012), M['carbon'], 0.004, rot=(0, s * 0.3, s * 0.25)))
            splitter.append(box('splitter_rod', (s * fw * 0.5, yf - 0.05, zf + 0.04), (0.012, 0.012, 0.12), M['trim']))
        self.variant('aero_f', 'splitter', splitter, stock=(stock_front == 'splitter'))
        self.variant('skirt', 'none', [], stock=(stock_side == 'none'))
        y0, y1 = self.wb / 2 - self.r - 0.12, -self.wb / 2 + self.r + 0.12
        self.variant('skirt', 'aero', [b.patch('side_skirt', y0, y1, 1.0, 2.6, M['carbon'], off=0.02, ny=10, nt=3)],
                     stock=(stock_side == 'aero'))
        rw = st[-1][1]
        zr = st[-1][2]
        self.variant('aero_r', 'none', [], stock=(stock_rear == 'none'))
        dif = [box('diffuser', (0, yr + 0.2, zr - 0.02), (rw * 1.6, 0.5, 0.02), M['carbon'], 0.004, rot=(-0.18, 0, 0))]
        for k in range(-3, 4):
            dif.append(box('diffuser_fin', (k * rw * 0.22, yr + 0.12, zr + 0.02), (0.012, 0.36, 0.1), M['carbon'], rot=(-0.18, 0, 0)))
        self.variant('aero_r', 'diffuser', dif, stock=(stock_rear == 'diffuser'))

    def weapons(self):
        """Weapon slot (D-064): fun garage weapons, each firing through the console's red button."""
        b, M = self.b, self.M
        st = b.st
        self.variant('weapon', 'none', [], stock=True)
        ym = (self.y_roof0 + self.y_rw0) / 2
        zr = self.top_z(0, ym)
        gun = [box('mg_base', (0, ym, zr + 0.02), (0.3, 0.3, 0.04), M['grey'], 0.01),
               cyl('mg_turret', (0, ym, zr + 0.08), 0.1, 0.08, M['dark'], rot=(0, 0, 0), verts=16),
               box('mg_body', (0, ym + 0.05, zr + 0.18), (0.14, 0.34, 0.12), M['dark'], 0.02),
               box('mg_ammo', (0.12, ym - 0.02, zr + 0.16), (0.08, 0.16, 0.1), M['accent'], 0.01),
               box('mg_sight', (0, ym - 0.02, zr + 0.26), (0.03, 0.06, 0.05), M['grey'], 0.005)]
        for k in range(6):
            a = 2 * math.pi * k / 6
            gun.append(cyl('mg_barrel', (math.cos(a) * 0.028, ym + 0.42, zr + 0.18 + math.sin(a) * 0.028), 0.01, 0.5, M['grey'], rot=(math.pi / 2, 0, 0), verts=8))
        gun.append(torus('mg_clamp', (0, ym + 0.55, zr + 0.18), 0.04, 0.008, M['dark'], rot=(math.pi / 2, 0, 0), seg=16, mseg=4))
        self.variant('weapon', 'minigun', gun)
        pod = [box('rk_rail', (0, ym, zr + 0.03), (0.6, 0.5, 0.05), M['grey'], 0.01)]
        for sx in (-0.2, 0.2):
            pod.append(box('rk_pod', (sx, ym, zr + 0.16), (0.2, 0.52, 0.2), M['dark'], 0.03, seg=2))
            for k in range(4):
                p = (sx + (k % 2 - 0.5) * 0.09, ym + 0.265, zr + 0.16 + (k // 2 - 0.5) * 0.09)
                pod.append(cyl('rk_tube', p, 0.035, 0.02, M['well'], rot=(math.pi / 2, 0, 0), verts=12))
                pod.append(cyl('rk_tip', (p[0], p[1] - 0.012, p[2]), 0.02, 0.03, M['tail'], rot=(math.pi / 2, 0, 0), verts=10))
            pod.append(box('rk_stripe', (sx, ym, zr + 0.265), (0.2, 0.4, 0.01), M['amber'], 0.002))
        self.variant('weapon', 'rockets', pod)
        rz = st[-1][2] + 0.28
        ry = self.fy(0, rz, False)
        oil = [cyl('oil_drum', (0, ry - 0.2, rz), 0.13, 0.5, M['accent'], rot=(0, math.pi / 2, 0), verts=18),
               torus('oil_band', (-0.12, ry - 0.2, rz), 0.13, 0.01, M['grey'], rot=(0, math.pi / 2, 0), seg=18, mseg=4),
               torus('oil_band', (0.12, ry - 0.2, rz), 0.13, 0.01, M['grey'], rot=(0, math.pi / 2, 0), seg=18, mseg=4),
               box('oil_bracket', (0, ry - 0.08, rz - 0.1), (0.5, 0.2, 0.04), M['grey'], 0.01),
               cyl('oil_nozzle', (0, ry - 0.36, rz - 0.08), 0.03, 0.14, M['dark'], rot=(math.pi / 2, 0, 0), verts=10),
               box('oil_warning', (0, ry - 0.2, rz + 0.13), (0.12, 0.12, 0.004), M['amber'])]
        self.variant('weapon', 'oil', oil)
        fz = st[0][2] + 0.18
        fy = self.fy(0, fz)
        spikes = [box('ram_bar', (0, fy + 0.08, fz), (st[0][1] * 2.1, 0.08, 0.08), M['grey'], 0.02)]
        for k in range(7):
            x = (k - 3) * st[0][1] * 0.3
            bpy.ops.mesh.primitive_cone_add(vertices=8, radius1=0.04, radius2=0.0, depth=0.18, location=(x, fy + 0.21, fz), rotation=(-math.pi / 2, 0, 0))
            o = bpy.context.object
            o.name = 'ram_spike'
            o.data.materials.append(M['chrome'])
            spikes.append(o)
        self.variant('weapon', 'spikes', spikes)

    def hoods(self, stock, y0, y1):
        b, M = self.b, self.M
        self.variant('hood', 'stock', [], stock=(stock == 'stock'))
        vent = [b.patch('hood_vent', y0, y1, 8.9, 10.0, M['trim'], off=0.008, ny=4, nt=4)]
        for k in range(5):  # louvres across the vent
            yy = y0 + (y1 - y0) * (k + 0.5) / 5
            vent.append(b.line_across('louvre', yy, 8.95, 10.0, M['grey'], 0.016))
        self.variant('hood', 'vent', vent, stock=(stock == 'vent'))
        self.variant('hood', 'carbon', [b.patch('hood_carbon', self.front - 0.12, self.y_ws0 + 0.04, 6.2, 10.0, M['carbon'], off=0.005, ny=12, nt=8)],
                     stock=(stock == 'carbon'))

    def arch_flare(self, name, prof, m, yc, s):
        """A flare swept round one wheel opening (D-075, prototyped live in Blender through MCP):
        prof is a closed (outward x, radial) section; it follows the body side round the arch."""
        b = self.b
        R = self.r + 0.09
        rings = []
        for k in range(33):
            a = math.radians(-105 + 210 * k / 32)
            y0, z0 = yc + math.sin(a) * R, self.r + math.cos(a) * R
            if z0 < b.st[0][2] + 0.02:
                continue
            x = b.side_x(y0 + math.sin(a) * 0.05, z0 + math.cos(a) * 0.05, s)
            if x is None:
                continue
            rad = Vector((0, math.sin(a), math.cos(a)))
            rings.append([Vector((x + s * o_, y0, z0)) + rad * r_ for o_, r_ in prof])
        if len(rings) < 4:
            return None
        n = len(prof)
        verts = [tuple(v) for r in rings for v in r]
        faces = []
        for i in range(len(rings) - 1):
            for j in range(n):
                a_, b_ = i * n + j, i * n + (j + 1) % n
                f = (a_, b_, b_ + n, a_ + n)
                faces.append(f if s > 0 else f[::-1])
        o = link(name, verts, faces, [m])
        for p_ in o.data.polygons:
            p_.use_smooth = True
        return o

    def arch_lips(self):
        """Pressed-steel arch flares in paint (stock) and bolt-on black rally flares (variant)."""
        M = self.M
        steel = [(-0.01, 0.06), (0.015, 0.045), (0.03, 0.02), (0.032, -0.005), (0.018, -0.012), (-0.01, -0.005)]
        rally = [(-0.01, 0.09), (0.035, 0.08), (0.06, 0.04), (0.062, -0.01), (0.045, -0.02), (-0.01, -0.01)]
        wide = [(-0.01, 0.13), (0.05, 0.12), (0.095, 0.07), (0.105, 0.0), (0.085, -0.025), (-0.01, -0.015)]
        bolt_on, widebody = [], []
        for yc in (self.wb / 2, -self.wb / 2):
            for s in (-1, 1):
                o = self.arch_flare('arch_flare', steel, M['paint'], yc, s)
                if o:
                    self.add(o)
                o = self.arch_flare('rally_flare', rally, M['trim'], yc, s)
                if o:
                    bolt_on.append(o)
                    for k in range(5):  # rivets along the flare
                        a = math.radians(-80 + 160 * k / 4)
                        y, z = yc + math.sin(a) * (self.r + 0.17), self.r + math.cos(a) * (self.r + 0.17)
                        x = self.b.side_x(y, z, s)
                        if x is not None and z > self.b.st[0][2] + 0.05:
                            bolt_on.append(cyl('flare_rivet', (x + s * 0.05, y, z), 0.008, 0.012, M['chrome'], rot=(0, math.pi / 2, 0), verts=8))
                # widebody over-fenders (D-080): wide, body colour, a row of exposed bolts - the modern
                # bolt-on kit look
                o = self.arch_flare('wide_flare', wide, M['paint'], yc, s)
                if o:
                    widebody.append(o)
                    R = self.r + 0.09  # on the flare's own outer face: profile point (0.095 out, 0.07 radial)
                    for k in range(9):
                        a = math.radians(-88 + 176 * k / 8)
                        x = self.b.side_x(yc + math.sin(a) * (R + 0.05), self.r + math.cos(a) * (R + 0.05), s)
                        y, z = yc + math.sin(a) * (R + 0.07), self.r + math.cos(a) * (R + 0.07)
                        if x is not None and z > self.b.st[0][2] + 0.05:
                            widebody.append(cyl('wide_bolt', (x + s * 0.1, y, z), 0.011, 0.014, M['chrome'], rot=(0, math.pi / 2, 0), verts=8))
        self.variant('flares', 'none', [], stock=True)
        self.variant('flares', 'rally', bolt_on)
        self.variant('flares', 'widebody', widebody)

    def realism(self, rub_strip=False, two_tone=True):
        """Details that read as a real car at race distance: brake calipers behind the spokes, window
        rubber and a roof drip rail, a dark lower sill, optional side rub strip."""
        b, M = self.b, self.M
        # calipers: fixed to the body (they do not spin), at the trailing top of each disc
        for yc in (self.wb / 2, -self.wb / 2):
            for s in (-1, 1):
                x = s * (self.track / 2 + self.width / 2 - 0.1)
                a = math.radians(-35 if yc > 0 else 35)
                rr = self.r * 0.7 * 0.62
                q = Vector((x, yc - math.sin(a) * rr, self.r + math.cos(a) * rr))
                cal = box('caliper', q, (0.05, 0.12, 0.07), M['caliper'], 0.012)
                cal.rotation_euler = (a, 0, 0)
                self.add(cal)
        # window rubber: thin dark seal along the top of the greenhouse, drip rail on the roof edge
        self.add(b.line_along('drip_rail', self.y_ws0 - 0.05, self.y_rw1 + 0.05, 7.9, M['trim'], 0.018))
        self.add(b.line_along('glass_seal', self.y_ws0 - 0.03, self.y_rw1 + 0.03, 7.6, M['trim'], 0.012))
        if two_tone:  # dark lower sill between the arches, like a real car's stone guard
            y0, y1 = self.wb / 2 - self.r - 0.08, -self.wb / 2 + self.r + 0.08
            self.add(b.patch('sill_guard', y0, y1, 1.5, 2.9, M['dark'], off=0.004, ny=12, nt=3))
        if rub_strip:
            self.add(b.patch('rub_strip', self.front - 0.5, self.rear + 0.5, 4.0, 4.12, M['trim'], off=0.01, ny=24, nt=1))

    def real_details(self, hatch=False, drive='FWD'):
        """Everything else a real car has (owner: every detail a real car has): cowl panel and
        washer jets, windscreen shade band, bumper seams, badges, side markers, reflectors, high
        brake light, plate lamp, tow eyes, key barrels, mirror repeaters, interior mirror, sun
        visors, seat belts, handbrake, rear wiper, suspension in the arches and an underbody."""
        b, M = self.b, self.M
        st = b.st
        # windscreen base: black cowl panel with a vent slot row, two washer jets on the bonnet
        self.add(b.patch('cowl', self.y_ws0 + 0.02, self.y_ws0 - 0.07, 6.3, 10.0, M['trim'], off=0.006, ny=2, nt=8))
        for k in range(8):
            self.add(b.line_across('cowl_slot', self.y_ws0 - 0.02, 7.0 + 0.35 * k, 7.25 + 0.35 * k, M['well'], 0.012))
        for sx in (-0.3, 0.3):
            p = b.surf(self.y_ws0 + 0.12, 9.4, 0.006, 1 if sx > 0 else -1)
            self.add(box('washer_jet', Vector((sx, p.y, p.z + 0.006)), (0.02, 0.03, 0.012), M['trim'], 0.004))
        # tinted shade band along the top of the windscreen
        self.add(b.patch('shade_band', self.y_roof0 + 0.12, self.y_roof0 + 0.02, 8.2, 10.0, M['trim'], off=0.004, ny=2, nt=6))
        # bumper-to-wing seams
        for y in (self.front - 0.34, self.rear + 0.3):
            self.add(b.line_across('bumper_seam', y, 2.2, 5.6, M['line']))
        # badges: original emblem (chrome diamond) front and rear, a model script, wing badges
        zf = st[0][3] - 0.02
        yf = self.fy(0, zf) + 0.01
        dia = box('badge', (0, yf, zf), (0.06, 0.012, 0.06), M['chrome'], 0.006)
        dia.rotation_euler = (0, math.radians(45), 0)
        self.add(dia)
        zr = st[-1][3] - 0.2
        yr = self.fy(0, zr, False) - 0.01
        dia = box('badge', (0, yr, zr), (0.06, 0.012, 0.06), M['chrome'], 0.006)
        dia.rotation_euler = (0, math.radians(45), 0)
        self.add(dia)
        self.add(box('model_script', (0.45, self.fy(0.45, zr - 0.06, False) - 0.01, zr - 0.06), (0.22, 0.01, 0.025), M['chrome'], 0.004))
        for s in (-1, 1):
            p = b.surf(self.wb / 2 - self.r - 0.2, 4.6, 0.008, s)
            self.add(box('wing_badge', p, (0.008, 0.1, 0.025), M['chrome'], 0.003))
            p = b.surf(self.front - 0.2, 3.6, 0.006, s)
            self.add(box('marker_f', p, (0.01, 0.06, 0.025), M['amber_l' if s < 0 else 'amber_r'], 0.004))
            p = b.surf(self.rear + 0.18, 3.6, 0.006, s)
            self.add(box('marker_r', p, (0.01, 0.06, 0.025), M['tail'], 0.004))
            zz = st[-1][2] + 0.14
            xx = s * st[-1][1] * 0.72
            self.add(box('reflector_r', (xx, self.fy(xx, zz, False) - 0.008, zz), (0.12, 0.012, 0.03), M['tail_dim'], 0.004))
            p = b.surf(self.y_ws0 - 0.2, 6.3, 0.0, s)
            self.add(box('mirror_rep', p + Vector((s * 0.3, 0.066, 0.1)), (0.1, 0.008, 0.024), M['amber_l' if s < 0 else 'amber_r']))
            p = b.surf(self.y_ws0 - 0.7, 5.45, 0.01, s)
            self.add(cyl('key', p + Vector((0, 0.12, 0)), 0.01, 0.01, M['chrome'], verts=10))
        # high-mounted brake light on the top of the rear glass, plate lamp, tow eyes
        p = b.surf(self.y_rw0 - 0.05, 9.8, 0.012, 1)
        self.add(box('brake_high', Vector((0, p.y, p.z)), (0.34, 0.03, 0.025), M['tail'], 0.006))
        zl = st[-1][2] + 0.4
        self.add(box('plate_lamp', (0, self.fy(0, zl, False) - 0.02, zl), (0.12, 0.02, 0.015), M['lamp']))
        for front in (True, False):
            z = (st[0] if front else st[-1])[2] + 0.12
            y = self.fy(0.42, z, front) + (0.02 if front else -0.02)
            self.add(torus('tow_eye', (0.42, y, z), 0.025, 0.007, M['grey'], rot=(0, 0, 0), seg=12, mseg=4))
        if hatch:  # rear wiper across the hatch glass
            p = b.surf(self.y_rw1 + 0.06, 10.0, 0.012, 1)
            self.add(box('rear_wiper', Vector((0.18, p.y, p.z)), (0.38, 0.014, 0.012), M['trim']))
            self.add(cyl('rear_wiper_hub', Vector((0, p.y, p.z)), 0.02, 0.02, M['trim'], rot=(0, 0, 0), verts=10))
        zs = min(t[2] for t in st)
        # suspension inside the arches: strut, coil spring, lower arm inboard of each tyre
        for yc in (self.wb / 2, -self.wb / 2):
            for s in (-1, 1):
                x = s * (self.track / 2 - self.width / 2 - 0.1)
                self.add(cyl('strut', (x, yc, self.r + 0.3), 0.025, 0.5, M['grey'], rot=(0, 0, 0), verts=10))
                turns = 5
                pts = [Vector((x + 0.06 * math.cos(a), yc + 0.06 * math.sin(a), self.r + 0.12 + 0.3 * a / (2 * math.pi * turns)))
                       for a in [i * 0.35 for i in range(int(2 * math.pi * turns / 0.35))]]
                self.add(tube_along('coil', pts, 0.008, M['spring'], sides=5))
                self.add(box('control_arm', (x * 0.7, yc, self.r - 0.08), (abs(x) * 0.6, 0.05, 0.03), M['dark']))
        # underbody: fuel tank, oil pan, driveshaft (RWD/AWD), exhaust run and silencer
        self.add(box('fuel_tank', (0, -self.wb / 2 + 0.55, zs + 0.08), (0.9, 0.5, 0.18), M['dark'], 0.02))
        self.add(box('oil_pan', (0, self.wb / 2 + 0.1, zs + 0.02), (0.4, 0.45, 0.12), M['dark'], 0.02))
        if drive in ('RWD', 'AWD'):
            self.add(cyl('driveshaft', (0, 0, zs + 0.08), 0.04, self.wb - 0.6, M['grey'], rot=(math.pi / 2, 0, 0), verts=10))
        self.add(cyl('exhaust_run', (0.25, 0.0, zs + 0.05), 0.035, self.wb * 0.9, M['grey'], rot=(math.pi / 2, 0, 0), verts=10))
        self.add(box('muffler', (0.3, -self.wb / 2 - 0.35, zs + 0.08), (0.34, 0.45, 0.16), M['grey'], 0.03))

    # ---- lamps
    def headlamp_patch(self, y0, y1, t0, t1, round_lamps=2):
        """Headlamp cluster on the curved nose: dark housing, chrome reflector, projector lenses
        (spheres need no orientation on a curved surface), a DRL strip, a clear lens over it all."""
        b, M = self.b, self.M
        self.add(b.patch('head_housing', y0, y1, t0, t1, M['trim'], off=0.003, ny=5, nt=5))
        pad = 0.12
        self.add(b.patch('head_reflector', y0 - 0.01, y1 + 0.01, t0 + pad, t1 - pad, M['reflector'], off=0.005, ny=4, nt=4))
        for s in (-1, 1):
            for k in range(round_lamps):
                t = t0 + pad + (t1 - t0 - 2 * pad) * (k + 0.5) / round_lamps
                p = b.surf(y0 + (y1 - y0) * 0.45, t, 0.004, s)
                bpy.ops.mesh.primitive_uv_sphere_add(segments=16, ring_count=8, radius=0.038, location=p)
                o = bpy.context.object
                o.name = 'projector'
                o.data.materials.append(M['lamp'])
                self.add(o)
        self.add(b.patch('drl', y0 + (y1 - y0) * 0.78, y0 + (y1 - y0) * 0.9, t0 + 0.05, t1 - 0.05, M['lamp'], off=0.008, ny=1, nt=6))
        self.add(b.patch('head_lens', y0 + 0.005, y1 - 0.005, t0 - 0.03, t1 + 0.03, M['lens'], off=0.03, ny=5, nt=5))

    def face_lamps(self, x, z, wdt, hgt, front=True, style='quad'):
        """Lamps on the planar nose/tail face (the loft caps)."""
        M = self.M
        d = 1 if front else -1
        if front:
            self.head_spec = (x, z, wdt, hgt)
        for s in (-1, 1):
            cx = s * x
            y = min(self.fy(cx + s * wdt * 0.4, z, front), self.fy(cx - s * wdt * 0.4, z, front)) if front else                 max(self.fy(cx + s * wdt * 0.4, z, front), self.fy(cx - s * wdt * 0.4, z, front))
            self.add(box('lamp_bezel', (cx, y + d * 0.008, z), (wdt + 0.03, 0.02, hgt + 0.03), M['trim'], 0.008))
            if front:
                self.add(box('lamp_back', (cx, y + d * 0.014, z), (wdt, 0.012, hgt), M['reflector']))
                for k in range(2 if style == 'quad' else 1):
                    ox = (k - 0.5) * wdt * 0.5 if style == 'quad' else 0
                    self.add(cyl('lamp_bowl', (cx + ox, y + d * 0.022, z), min(hgt, wdt / 2) * 0.42, 0.01, M['lamp'], rot=(math.pi / 2, 0, 0), verts=18))
                self.add(box('lamp_indicator', (cx + s * wdt * 0.36, y + d * 0.022, z - hgt * 0.25), (wdt * 0.22, 0.01, hgt * 0.35), M['amber_l' if s < 0 else 'amber_r']))
            else:
                # tail: brake, indicator, reverse chambers split by dark ribs
                self.add(box('tail_brake', (cx + s * wdt * 0.12, y + d * 0.016, z), (wdt * 0.72, 0.012, hgt), M['tail']))
                self.add(box('tail_ind', (cx - s * wdt * 0.4, y + d * 0.016, z + hgt * 0.22), (wdt * 0.16, 0.012, hgt * 0.5), M['amber_l' if s < 0 else 'amber_r']))
                self.add(box('tail_rev', (cx - s * wdt * 0.4, y + d * 0.016, z - hgt * 0.25), (wdt * 0.16, 0.012, hgt * 0.4), M['reverse']))
                for k in range(3):
                    self.add(box('tail_rib', (cx + s * wdt * (-0.22 + 0.22 * k), y + d * 0.02, z), (0.008, 0.008, hgt), M['trim']))
            self.add(box('lamp_lens', (cx, y + d * 0.03, z), (wdt, 0.006, hgt), M['lens']))

    def grille(self, z, wdt, hgt, style='slats'):
        M = self.M
        y = min(self.fy(-wdt / 2, z), self.fy(wdt / 2, z), self.fy(0, z + hgt / 2), self.fy(0, z - hgt / 2))
        self.add(box('grille_surround', (0, y + 0.01, z), (wdt + 0.04, 0.02, hgt + 0.04), M['trim'], 0.01))
        self.add(box('grille_back', (0, y + 0.004, z), (wdt, 0.012, hgt), M['well']))
        if style == 'slats':
            for k in range(4):
                self.add(box('grille_slat', (0, y + 0.018, z - hgt / 2 + hgt * (k + 0.5) / 4), (wdt - 0.02, 0.012, 0.012), M['chrome']))
        else:  # honeycomb-like mesh of small cells
            nx, nz = int(wdt / 0.05), max(2, int(hgt / 0.05))
            for i in range(nx):
                for j in range(nz):
                    ox = 0.025 if j % 2 else 0
                    xx = -wdt / 2 + (i + 0.5) * wdt / nx + ox
                    if abs(xx) < wdt / 2 - 0.015:
                        self.add(box('grille_cell', (xx, y + 0.016, z - hgt / 2 + (j + 0.5) * hgt / nz), (0.03, 0.01, 0.024), M['grey']))

    def bumpers(self, front_band, rear_band, m, out=0.035, thick=0.03):
        """Wrap-around bumpers (D-074, prototyped live through Blender MCP): a band that follows the
        body outline round the nose / tail and the corners back to the wheel arches, `out` outside the
        skin, with rounded top and bottom lips. 80s cars carry them in black plastic; the bare painted
        panel read as a toy. Bands are (z0, z1)."""
        bvh = self.b.bvh
        self.bumper_band = {True: (front_band[0], front_band[1], out + thick), False: (rear_band[0], rear_band[1], out + thick)}
        for front, (z0, z1) in ((True, front_band), (False, rear_band)):
            # rays from just inside the overhang, swept round the end: they end at the wheel arches
            cy = (self.front - 0.25) if front else (self.rear + 0.25)
            steps = 48
            rows = []
            for z, extra in ((z0, -0.012), (z0 + 0.018, 0.0), (z1 - 0.018, 0.0), (z1, -0.014)):
                row = []
                for k in range(steps + 1):
                    a = math.radians(6 + 168 * k / steps)
                    d = Vector((math.cos(a), math.sin(a) if front else -math.sin(a), 0))
                    hit = bvh.ray_cast(Vector((0, cy, z)) + d * 4.0, -d, 4.0)
                    if hit[0] is None:
                        row.append(None)
                        continue
                    nrm = Vector((hit[1].x, hit[1].y, 0))
                    nrm = nrm.normalized() if nrm.length > 1e-3 else d
                    row.append(hit[0] + nrm * (out + extra))
                rows.append(row)
            cols = [k for k in range(steps + 1) if all(r[k] is not None for r in rows)]
            verts, faces, idx = [], [], {}
            for ri, r in enumerate(rows):
                for k in cols:
                    idx[(ri, k)] = len(verts)
                    verts.append(tuple(r[k]))
            for ri in range(len(rows) - 1):
                for a, b2 in zip(cols, cols[1:]):
                    f = (idx[(ri, a)], idx[(ri, b2)], idx[(ri + 1, b2)], idx[(ri + 1, a)])
                    faces.append(f[::-1] if front else f)
            o = link('bumper_front' if front else 'bumper_rear', verts, faces, [m])
            sol = o.modifiers.new('thick', 'SOLIDIFY')
            sol.thickness = thick
            sol.offset = -1
            apply_mod(o, sol)
            for p_ in o.data.polygons:
                p_.use_smooth = True
            self.add(o)
            # a rubbing strip along the face of the bumper
            zm = (z0 + z1) / 2
            self.add(box('bumper_strip', (0, (self.fy(0, zm, front) + (0.004 if front else -0.004)), zm), (0.9, 0.012, 0.022), self.M['grey'], 0.006))

    def intercoolers(self):
        """Front-mount intercooler seen through the centre lower opening when a turbo is fitted
        (D-072): an aluminium core of fins between two end tanks; the race one is bigger and shows
        the charge pipes. Parts PART_perf_turbo_<level>, kept by the game when that turbo is fitted."""
        if not hasattr(self, 'centre_intake'):
            return
        M = self.M
        z, wdt, hgt = self.centre_intake
        y = min(self.fy(-wdt / 2, z), self.fy(wdt / 2, z), self.fy(0, z))
        self.variant('perf_turbo', 'none', [], stock=True)
        for level, k in (('street', 0.78), ('race', 0.96)):
            cw, chh = wdt * k, hgt * min(1.0, k + 0.1)
            yc = y + 0.012
            parts = [box('ic_frame', (0, yc, z), (cw + 0.02, 0.012, chh + 0.012), M['alu'] if 'alu' in M else M['grey'], 0.003)]
            n = max(8, int(cw / 0.012))
            for i in range(n):  # vertical fins, dark gaps between them read as a core
                parts.append(box('ic_fin', (-cw / 2 + (i + 0.5) * cw / n, yc + 0.007, z), (0.004, 0.006, chh - 0.006), M['grey']))
            for sgn in (-1, 1):
                parts.append(box('ic_tank', (sgn * (cw / 2 + 0.01), yc + 0.004, z), (0.024, 0.02, chh + 0.01), M['chrome'], 0.006))
                if level == 'race':  # charge pipes turning up out of sight behind the bumper
                    parts.append(tube_along('ic_pipe', [Vector((sgn * (cw / 2 + 0.01), yc - 0.01, z + chh / 2)),
                                                        Vector((sgn * (cw / 2 + 0.01), yc - 0.05, z + chh / 2 + 0.06))], 0.022, M['chrome'], sides=10))
            self.variant('perf_turbo', level, parts)

    def intake(self, x, z, wdt, hgt, fog=False):
        M = self.M
        if not x:  # the centre lower opening: where a front-mount intercooler shows (D-072)
            self.centre_intake = (z, wdt, hgt)
        for s in ((-1, 1) if x else (1,)):
            cx = s * x
            y = min(self.fy(cx - wdt / 2, z), self.fy(cx + wdt / 2, z))
            self.add(box('intake_frame', (cx, y + 0.008, z), (wdt + 0.03, 0.016, hgt + 0.03), M['trim'], 0.008))
            self.add(box('intake_back', (cx, y + 0.003, z), (wdt, 0.012, hgt), M['well']))
            for k in range(2):
                self.add(box('intake_bar', (cx, y + 0.016, z - hgt / 4 + k * hgt / 2), (wdt - 0.01, 0.01, 0.01), M['trim']))
            if fog:
                self.add(cyl('fog', (cx, y + 0.02, z), hgt * 0.3, 0.012, M['lamp'], rot=(math.pi / 2, 0, 0), verts=16))
                self.add(torus('fog_ring', (cx, y + 0.024, z), hgt * 0.32, 0.006, M['chrome'], rot=(math.pi / 2, 0, 0), seg=18, mseg=4))

    def modern_mods(self):
        """D-080: roof (scoop / rally rack / shark fin) and light signature (LED DRL blades / full-width
        light bar) slots, from modern cars - the GR Yaris and WRX roof scoops, rally roof racks, the
        L-shaped DRLs and full-width bars of current lamps."""
        b, M = self.b, self.M
        y0, y1 = self.y_roof0, self.y_rw0
        ms = getattr(self, 'mod_stock', {})  # a car can ship with a roof / light kit fitted (D-081)
        self.variant('roof', 'none', [], stock=ms.get('roof', 'none') == 'none')
        ys = y0 - 0.22
        zs = self.top_z(0, ys)
        self.variant('roof', 'scoop', stock=ms.get('roof') == 'scoop', objs=[box('roof_scoop', (0, ys, zs + 0.035), (0.34, 0.3, 0.07), M['paint'], 0.03, seg=2),
                                       box('scoop_mouth', (0, ys + 0.15, zs + 0.04), (0.28, 0.012, 0.045), M['well']),
                                       box('scoop_lip', (0, ys + 0.152, zs + 0.066), (0.3, 0.02, 0.008), M['trim'], 0.003)])
        ym = (y0 + y1) / 2
        zr = min(self.top_z(-0.4, ym), self.top_z(0.4, ym))
        rack = []
        for sx in (-0.45, 0.45):
            rack.append(box('rack_rail', (sx, ym, zr + 0.06), (0.03, (y0 - y1) * 0.9, 0.03), M['trim'], 0.008))
            for yy in (y0 - 0.12, y1 + 0.12):
                rack.append(box('rack_foot', (sx, yy, zr + 0.025), (0.05, 0.07, 0.05), M['trim'], 0.01))
        for k in range(4):
            rack.append(box('rack_bar', (0, y1 + 0.12 + (y0 - y1 - 0.24) * k / 3, zr + 0.08), (0.96, 0.025, 0.025), M['grey'], 0.006))
        rack.append(cyl('rack_tyre', (0, ym - 0.12, zr + 0.16), 0.3, 0.16, M['tyre'], rot=(0, 0, 0), verts=24))
        rack.append(cyl('rack_tyre_rim', (0, ym - 0.12, zr + 0.245), 0.19, 0.012, M['grey'], rot=(0, 0, 0), verts=20))
        for sx in (-0.33, -0.11, 0.11, 0.33):  # rally lamp pods on the front bar
            q = Vector((sx, y0 - 0.06, zr + 0.16))
            rack.append(cyl('rack_pod', q, 0.07, 0.07, M['trim'], rot=(math.pi / 2, 0, 0), verts=16))
            rack.append(cyl('rack_pod_lens', q + Vector((0, 0.037, 0)), 0.06, 0.006, M['lamp'], rot=(math.pi / 2, 0, 0), verts=16))
            rack.append(box('rack_pod_x', q + Vector((0, 0.042, 0)), (0.11, 0.004, 0.012), M['trim'], rot=(0, 0.785, 0)))
        self.variant('roof', 'rack', rack)
        yf = y1 + 0.08
        zf = self.top_z(0, yf)
        fin = link('shark_fin', [(-0.04, yf + 0.13, zf), (0.04, yf + 0.13, zf), (0.04, yf - 0.13, zf), (-0.04, yf - 0.13, zf),
                                 (-0.006, yf - 0.08, zf + 0.1), (0.006, yf - 0.08, zf + 0.1)],
                   [(0, 1, 5, 4), (3, 4, 5, 2), (1, 2, 5), (0, 4, 3), (0, 3, 2, 1)], [M['paint']])
        fix_normals(fin)
        self.variant('roof', 'fin', [fin])
        # light signature on the nose
        self.variant('lights', 'stock', [], stock=ms.get('lights', 'stock') == 'stock')
        hs = getattr(self, 'head_spec', None)
        if hs:
            x, z, wdt, hgt = hs
            drl, bar = [], []
            for sd in (-1, 1):
                cx = sd * x
                yb = self.fy(cx, z - hgt / 2 - 0.04)
                drl.append(box('drl_blade', (cx, yb + 0.012, z - hgt / 2 - 0.04), (wdt + 0.02, 0.014, 0.032), M['drl'], 0.004))
                yo = self.fy(cx + sd * (wdt / 2 + 0.03), z)
                drl.append(box('drl_hook', (cx + sd * (wdt / 2 + 0.03), yo + 0.012, z - 0.01), (0.03, 0.014, hgt + 0.02), M['drl'], 0.004))
            zb = z + hgt / 2 + 0.045
            for k in range(13):  # the bar follows the nose's curve in short segments
                xx = -x + 2 * x * k / 12
                bar.append(box('light_bar', (xx, self.fy(xx, zb) + 0.01, zb), (2 * x / 12 + 0.012, 0.016, 0.032), M['drl'], 0.003))
            for sd in (-1, 1):
                bar.append(box('drl_blade', (sd * x, self.fy(sd * x, z - hgt / 2 - 0.04) + 0.012, z - hgt / 2 - 0.04), (wdt + 0.02, 0.014, 0.03), M['drl'], 0.004))
            self.variant('lights', 'drl', drl, stock=ms.get('lights') == 'drl')
            self.variant('lights', 'bar', bar, stock=ms.get('lights') == 'bar')

    def build(self, spec):
        b, M = self.b, self.M
        self.intercoolers()
        self.modern_mods()
        # Join fixed details into BODY; variant parts stay separate, parented to BODY.
        body = join([b.obj] + self.fixed, 'BODY')
        for slot, vs in self.parts.items():
            for name, objs in vs.items():
                if not objs:
                    continue
                o = join(objs, f'PART_{slot}_{name}')
                for p in o.data.polygons:
                    p.use_smooth = False
                o.parent = body
        for name, (objs, loc, rot) in self.special.items():
            pivot = bpy.data.objects.new(name, None)
            bpy.context.scene.collection.objects.link(pivot)
            pivot.location = loc
            pivot.rotation_euler = rot
            bpy.context.view_layer.update()
            mesh = join(objs, 'MESH_' + name)
            mesh.parent = pivot
            mesh.matrix_parent_inverse = pivot.matrix_world.inverted()
            pivot.parent = body
        if getattr(self, 'eye', None):
            self.eye.parent = body
        for mk in getattr(self, 'markers', []):
            mk.parent = body
        for name, objs, start, direction in getattr(self, 'bones', []):
            pivot = bpy.data.objects.new(name, None)
            bpy.context.scene.collection.objects.link(pivot)
            pivot.location = start
            pivot.rotation_mode = 'QUATERNION'
            pivot.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(direction.normalized())
            bpy.context.view_layer.update()
            mesh = join(objs, 'MESH_' + name)
            mesh.parent = pivot  # authored in the pivot's own frame (along +Z from its origin)
            pivot.parent = body
        knob = bpy.data.objects.get('GEAR_KNOB')
        lever = bpy.data.objects.get('GEAR_LEVER')
        if knob and lever:
            bpy.context.view_layer.update()
            wm = knob.matrix_world.copy()
            knob.parent = lever
            knob.matrix_world = wm
        for ft, tag in getattr(self, 'foot_targets', []):
            pedal = bpy.data.objects.get(tag)
            if pedal:
                bpy.context.view_layer.update()
                wm = ft.matrix_world.copy()
                ft.parent = pedal
                ft.matrix_world = wm
        steer = bpy.data.objects.get('STEER_PIVOT')
        for g in getattr(self, 'grips', []):
            if steer:
                bpy.context.view_layer.update()
                wm = g.matrix_world.copy()
                g.parent = steer
                g.matrix_world = wm
        wheel_set(M, self.wb, self.track, self.r, self.width)
        CUSTOM.mkdir(parents=True, exist_ok=True)
        slots = {slot: {'variants': list(vs.keys()), 'stock': self.stock.get(slot, list(vs.keys())[0])} for slot, vs in self.parts.items()
                 if not slot.startswith('perf_')}  # performance parts follow the perf_<slot> upgrades (D-072)
        slots['rims'] = {'variants': RIMS, 'stock': spec['rim']}
        (CUSTOM / f'{self.id}.json').write_text(json.dumps({'schema_version': 1, 'car': self.id, 'slots': slots}, indent=1), encoding='utf-8')


# ---------------------------------------------------------------- the four class D cars

def mirror_quad(tag, c, w, h, m, car):
    """A mirror glass facing the rear (-Y) with a full 0..1 UV, so the live mirror picture fills it,
    and an empty `tag` at its centre the game aims the mirror camera and the driver's glance at."""
    c = Vector(c)
    verts = [c + Vector((-w / 2, 0, -h / 2)), c + Vector((w / 2, 0, -h / 2)), c + Vector((w / 2, 0, h / 2)), c + Vector((-w / 2, 0, h / 2))]
    o = link('mirror_glass', [tuple(v) for v in verts], [(0, 1, 2, 3)], [m])  # normal -Y (towards the driver)
    uv = o.data.uv_layers.new(name='UVMap')
    for poly in o.data.polygons:
        for li in poly.loop_indices:
            v = o.data.vertices[o.data.loops[li].vertex_index].co
            uv.data[li].uv = ((v.x - c.x) / w + 0.5, (v.z - c.z) / h + 0.5)
    e = bpy.data.objects.new(tag, None)
    bpy.context.scene.collection.objects.link(e)
    e.location = c
    car.markers = getattr(car, 'markers', []) + [e]
    return o


def split_driver_window(car, M):
    """The driver's door glass as its own part (DRIVER_WINDOW, D-064): the game winds it down into
    the door when the driver's arm goes out, and back up after."""
    b = car.b
    o = b.obj
    ys = car.y_roof0 - 0.45
    # the front door glass only: from the B-pillar (the cabin's b_pillar_trim sits at ys - 0.42) to the
    # windscreen - a wider cut took part of the rear side glass down with it on the fastbacks
    y_lo, y_hi = max(car.y_rw0 + 0.02, ys - 0.38), car.y_ws0 - 0.02
    bm = bmesh.new()
    bm.from_mesh(o.data)
    pick = [f for f in bm.faces if f.material_index == 1 and f.normal.x < -0.35 and y_lo < f.calc_center_median().y < y_hi]
    if not pick:
        bm.free()
        return
    verts = {}
    faces = []
    for f in pick:
        idx = []
        for v in f.verts:
            if v.index not in verts:
                verts[v.index] = (len(verts), tuple(v.co + Vector((0.004, 0, 0))))
            idx.append(verts[v.index][0])
        faces.append(tuple(idx))
    vl = [None] * len(verts)
    for _, (k, co) in verts.items():
        vl[k] = co
    bmesh.ops.delete(bm, geom=pick, context='FACES_ONLY')
    bm.to_mesh(o.data)
    bm.free()
    win = link('driver_window', vl, faces, [M['glass']])
    cx = sum(v[0] for v in vl) / len(vl)
    cy = sum(v[1] for v in vl) / len(vl)
    cz = min(v[2] for v in vl)
    car.animated('DRIVER_WINDOW', [win], (cx, cy, cz))
    # where the glass goes when wound down: its full height into the door, tucked 3.5 cm inboard of the
    # skin so it never shows below the belt line (the game reads this marker; heights differ per car)
    h = max(v[2] for v in vl) - cz
    drop = bpy.data.objects.new('WINDOW_DROP', None)
    bpy.context.scene.collection.objects.link(drop)
    drop.location = (cx + 0.035, cy, cz - h - 0.03)
    car.markers = getattr(car, 'markers', []) + [drop]
    # the window seal and a dark slot the glass disappears into
    car.add(b.line_along('window_seal', y_hi, y_lo, 6.05, M['trim'], 0.02))


def start(cid, stations, M, dims, bumper_m=None):
    b = Body(stations)
    b.build(M)
    b.cut_arches((dims[0] / 2, -dims[0] / 2), dims[2], dims[3], M)
    b.finish_surface()
    car = Car(cid, b, M, dims)
    split_driver_window(car, M)
    zb = min(st[3] for st in stations)
    car.add(b.cabin_lining(car.y_ws0 + 0.05, car.y_rw1 - 0.05, zb - 0.02, M))
    car.add(*b.pillars(car, M))
    # bumpers: black wrap-around bands low on the nose and tail (chrome on the truck), sized from
    # the end stations (D-074)
    f_zs, r_zs = stations[0][2], stations[-1][2]
    bands = {'D01': ((0.25, 0.40), (0.28, 0.44)), 'D02': ((0.24, 0.36), (0.28, 0.42)),
             'D03': ((0.24, 0.38), (0.28, 0.44)), 'D04': ((0.30, 0.46), (0.32, 0.48)), 'D05': ((0.22, 0.34), (0.27, 0.40))}
    fb, rb = bands.get(cid, ((f_zs, f_zs + 0.15), (r_zs + 0.03, r_zs + 0.19)))
    car.bumpers(fb, rb, bumper_m or (M['chrome'] if cid == 'D04' else M['trim']))
    return car


def build_d01():
    """Grip (FWD hot hatch): short overhangs, upright hatch, twin round lamps behind a clear lens,
    honeycomb grille, roof spoiler, five-spoke rims."""
    reset()
    M = palette((0.82, 0.08, 0.08), (0.95, 0.95, 0.95), (0.9, 0.9, 0.9), suit=(0.75, 0.1, 0.08))
    c = start('D01', [
        (2.025, 0.80, 0.25, 0.66, 0.72, 0.70, 'bonnet'),
        (1.95, 0.86, 0.23, 0.76, 0.81, 0.75, 'bonnet'),
        (1.70, 0.88, 0.22, 0.83, 0.87, 0.78, 'bonnet'),
        (1.25, 0.89, 0.22, 0.87, 0.91, 0.80, 'bonnet'),
        (0.80, 0.895, 0.22, 0.89, 0.95, 0.80, 'ws'),
        (0.10, 0.88, 0.22, 0.91, 1.42, 0.66, 'roof'),
        (-0.70, 0.88, 0.22, 0.92, 1.44, 0.68, 'roof'),
        (-1.40, 0.885, 0.22, 0.93, 1.42, 0.70, 'rw'),
        (-1.86, 0.89, 0.22, 0.95, 1.16, 0.76, 'deck'),
        (-1.98, 0.885, 0.23, 0.945, 1.10, 0.76, 'deck'),
        (-2.025, 0.865, 0.25, 0.93, 1.05, 0.74, None),
    ], M, (2.53, 1.67, 0.36, 0.20))
    c.glasshouse(b_pillars=[-0.55])
    c.doors([0.72, -0.52], [-0.4])
    c.panel_lines(bonnet_y=0.84, boot_y=-1.88, fuel_y=-1.25)
    c.mirrors()
    c.interior(rear_seats=True)
    c.driver()
    c.arch_lips()
    c.realism(rub_strip=c.id in ('D03', 'D04'))
    c.real_details(hatch=(c.id == 'D01'), drive={'D01': 'FWD', 'D02': 'RWD', 'D03': 'AWD', 'D04': 'RWD'}[c.id])
    c.face_lamps(0.5, 0.55, 0.3, 0.11, front=True, style='quad')
    c.face_lamps(0.52, 0.72, 0.36, 0.13, front=False)
    c.grille(0.52, 0.52, 0.1, style='honey')
    c.intake(0, 0.34, 0.8, 0.1)
    c.intake(0.55, 0.36, 0.18, 0.09, fog=True)
    c.plate(c.front, 0.46, True)
    c.plate(c.rear, 0.56, False)
    c.exhausts({'single': [(0.5, 0.045)], 'dual': [(0.44, 0.045), (0.56, 0.045)]}, 'single')
    c.spoilers('lip', 1.43, -1.42, 1.25)
    c.aero('none', 'none', 'none')
    c.weapons()
    c.hoods('stock', 1.6, 1.15)
    c.add(c.b.patch('stripe', 1.9, -1.9, 3.3, 3.55, M['accent'], off=0.005, ny=24, nt=1))
    c.build({'rim': 'five'})
    return 'D01'


def build_d02():
    """Drift (RWD fastback coupe): long low bonnet, closed pop-up lamps, slim bumper lamps, fastback
    glass, full-width tail bar, bronze dish rims."""
    reset()
    M = palette((0.92, 0.92, 0.94), (0.1, 0.35, 0.85), (0.72, 0.5, 0.18), suit=(0.1, 0.3, 0.75))
    c = start('D02', [
        (2.19, 0.78, 0.24, 0.56, 0.62, 0.70, 'bonnet'),
        (2.10, 0.86, 0.21, 0.70, 0.75, 0.78, 'bonnet'),
        (1.80, 0.905, 0.20, 0.80, 0.84, 0.80, 'bonnet'),
        (1.30, 0.915, 0.20, 0.84, 0.88, 0.82, 'bonnet'),
        (0.80, 0.915, 0.20, 0.86, 0.91, 0.82, 'ws'),
        (0.10, 0.90, 0.20, 0.88, 1.29, 0.62, 'roof'),
        (-0.62, 0.90, 0.20, 0.89, 1.31, 0.62, 'rw'),
        (-1.55, 0.915, 0.20, 0.90, 0.98, 0.80, 'deck'),
        (-2.05, 0.89, 0.22, 0.91, 0.97, 0.80, 'deck'),
        (-2.15, 0.85, 0.24, 0.89, 0.94, 0.76, 'deck'),
        (-2.19, 0.80, 0.27, 0.86, 0.91, 0.72, None),
    ], M, (2.62, 1.72, 0.36, 0.22))
    b = c.b
    c.glasshouse()
    c.doors([0.74, -0.64], [-0.5])
    c.panel_lines(bonnet_y=0.86, boot_y=-1.6, fuel_y=-1.2)
    for y in (2.02, 1.64):  # closed pop-up lids
        c.add(b.line_across('popup_cut', y, 7.05, 8.95, M['line']))
    c.add(b.line_along('popup_side', 2.02, 1.64, 7.05, M['line']))
    c.add(b.line_along('popup_side', 2.02, 1.64, 8.95, M['line']))
    c.mirrors()
    c.interior(rear_seats=True)
    c.driver()
    c.arch_lips()
    c.realism(rub_strip=c.id in ('D03', 'D04'))
    c.real_details(hatch=(c.id == 'D01'), drive={'D01': 'FWD', 'D02': 'RWD', 'D03': 'AWD', 'D04': 'RWD'}[c.id])
    c.face_lamps(0.6, 0.47, 0.3, 0.06, front=True, style='single')
    ty = max(c.fy(-0.7, 0.76, False), c.fy(0.7, 0.76, False))
    c.add(box('tail_panel', (0, ty - 0.008, 0.76), (1.44, 0.02, 0.14), M['trim'], 0.01))
    c.add(box('tail_bar', (0, ty - 0.02, 0.76), (1.36, 0.012, 0.09), M['tail']))
    for s in (-1, 1):
        c.add(box('tail_ind', (s * 0.62, ty - 0.026, 0.76), (0.1, 0.01, 0.09), M['amber_l' if s < 0 else 'amber_r']))
        c.add(box('tail_rev', (s * 0.44, ty - 0.026, 0.76), (0.08, 0.01, 0.09), M['reverse']))
    c.intake(0, 0.33, 0.9, 0.09)
    c.plate(c.front, 0.42, True)
    c.plate(c.rear, 0.58, False)
    c.exhausts({'single': [(-0.55, 0.05)], 'dual': [(-0.48, 0.05), (0.48, 0.05)], 'wide': [(-0.6, 0.065)]}, 'single')
    c.spoilers('lip', 0.99, -2.0, 1.3)
    c.aero('lip', 'none', 'none')
    c.weapons()
    c.hoods('stock', 1.45, 1.05)
    for y0, y1 in ((2.08, 0.9), (0.06, -0.6), (-1.58, -2.1)):  # twin stripes never cross glass
        c.add(b.patch('stripe', y0, y1, 9.25, 9.6, M['accent'], off=0.005, ny=10, nt=1))
    c.build({'rim': 'dish'})
    return 'D02'


def build_d03():
    """Balanced (AWD rally saloon): four doors, bonnet vent, big wing, mud flaps, gold mesh rims."""
    reset()
    M = palette((0.1, 0.28, 0.72), (0.98, 0.78, 0.1), (0.95, 0.75, 0.2), caliper=(0.95, 0.75, 0.1), suit=(0.95, 0.75, 0.1))
    c = start('D03', [
        (2.36, 0.80, 0.24, 0.62, 0.70, 0.72, 'bonnet'),
        (2.28, 0.90, 0.21, 0.76, 0.81, 0.82, 'bonnet'),
        (2.00, 0.945, 0.20, 0.84, 0.88, 0.85, 'bonnet'),
        (1.50, 0.955, 0.20, 0.87, 0.91, 0.86, 'bonnet'),
        (0.95, 0.955, 0.20, 0.89, 0.94, 0.86, 'ws'),
        (0.20, 0.94, 0.20, 0.90, 1.42, 0.68, 'roof'),
        (-0.95, 0.94, 0.20, 0.91, 1.43, 0.68, 'rw'),
        (-1.62, 0.95, 0.20, 0.93, 1.06, 0.84, 'deck'),
        (-2.22, 0.93, 0.22, 0.94, 1.04, 0.84, 'deck'),
        (-2.32, 0.89, 0.24, 0.92, 1.00, 0.80, 'deck'),
        (-2.36, 0.84, 0.27, 0.89, 0.96, 0.76, None),
    ], M, (2.86, 1.79, 0.36, 0.22))
    b = c.b
    c.glasshouse(b_pillars=[-0.36])
    c.doors([0.9, -0.34, -1.08], [-0.2, -0.95])
    c.panel_lines(bonnet_y=0.98, boot_y=-1.66, fuel_y=-1.35)
    c.mirrors()
    c.interior(rear_seats=True)
    c.driver()
    c.arch_lips()
    c.realism(rub_strip=c.id in ('D03', 'D04'))
    c.real_details(hatch=(c.id == 'D01'), drive={'D01': 'FWD', 'D02': 'RWD', 'D03': 'AWD', 'D04': 'RWD'}[c.id])
    c.face_lamps(0.56, 0.57, 0.34, 0.11, front=True, style='quad')
    c.face_lamps(0.6, 0.76, 0.32, 0.15, front=False)
    c.grille(0.55, 0.52, 0.1, style='honey')
    c.intake(0, 0.33, 0.7, 0.14)
    c.intake(0.62, 0.36, 0.2, 0.1, fog=True)
    c.plate(c.front, 0.46, True)
    c.plate(c.rear, 0.62, False)
    c.exhausts({'single': [(0.6, 0.055)], 'dual': [(0.52, 0.05), (0.66, 0.05)], 'quad': [(0.5, 0.042), (0.62, 0.042), (-0.5, 0.042), (-0.62, 0.042)]}, 'single')
    c.spoilers('wing', 1.05, -2.12, 1.5)
    c.aero('splitter', 'aero', 'diffuser')
    c.weapons()
    c.hoods('vent', 1.85, 1.4)
    for s in (-1, 1):
        for yy in (c.wb / 2 - 0.46, -c.wb / 2 - 0.46):
            c.add(box('mud_flap', (s * 0.8, yy, 0.22), (0.24, 0.015, 0.24), M['trim'], 0.01))
    c.add(b.patch('side_decal', 0.85, -1.05, 3.0, 4.2, M['accent'], off=0.005, ny=16, nt=3))
    c.build({'rim': 'mesh'})
    return 'D03'


def build_d04():
    """Heavy (RWD V8 street truck): lowered, tall cab, tonneau bed, chrome bar grille, ten-spoke rims."""
    reset()
    M = palette((0.1, 0.1, 0.12), (0.95, 0.45, 0.08), (0.22, 0.22, 0.24), suit=(0.95, 0.45, 0.08))
    c = start('D04', [
        (2.43, 0.86, 0.32, 0.82, 0.90, 0.80, 'bonnet'),
        (2.36, 0.95, 0.29, 0.94, 1.00, 0.88, 'bonnet'),
        (2.10, 0.985, 0.28, 1.00, 1.05, 0.91, 'bonnet'),
        (0.80, 0.99, 0.28, 1.04, 1.10, 0.92, 'ws'),
        (0.30, 0.98, 0.28, 1.06, 1.62, 0.80, 'roof'),
        (-0.55, 0.98, 0.28, 1.07, 1.64, 0.80, 'rw'),
        (-0.72, 0.99, 0.28, 1.07, 1.12, 0.95, 'bed'),
        (-2.28, 0.99, 0.28, 1.08, 1.12, 0.95, 'deck'),
        (-2.38, 0.95, 0.29, 1.06, 1.10, 0.92, 'deck'),
        (-2.43, 0.90, 0.31, 1.03, 1.07, 0.88, None),
    ], M, (2.96, 1.86, 0.40, 0.26))
    b = c.b
    c.glasshouse()
    c.doors([0.74, -0.52], [-0.4])
    c.panel_lines(bonnet_y=0.82, fuel_y=-1.2)
    c.add(b.line_across('bed_cut', -0.72, 6.1, 10.0, M['line']))
    c.add(b.line_across('tailgate_cut', -2.3, 6.1, 10.0, M['line']))
    c.mirrors()
    c.interior(rear_seats=False)
    c.driver()
    c.arch_lips()
    c.realism(rub_strip=c.id in ('D03', 'D04'))
    c.real_details(hatch=(c.id == 'D01'), drive={'D01': 'FWD', 'D02': 'RWD', 'D03': 'AWD', 'D04': 'RWD'}[c.id])
    c.grille(0.68, 1.1, 0.3, style='slats')
    c.face_lamps(0.74, 0.7, 0.26, 0.16, front=True, style='quad')
    c.face_lamps(0.82, 0.86, 0.14, 0.3, front=False)
    c.intake(0, 0.44, 0.9, 0.08)
    c.plate(c.front, 0.5, True)
    c.plate(c.rear, 0.7, False)
    c.exhausts({'dual': [(0.62, 0.06), (-0.62, 0.06)], 'side': [(0.8, 0.07)]}, 'dual')
    c.spoilers('none', 1.12, -2.3, 1.7)
    c.aero('none', 'none', 'none')
    c.weapons()
    c.hoods('stock', 1.9, 1.1)
    c.add(b.patch('bonnet_bulge', 2.0, 1.0, 9.2, 10.0, M['paint'], off=0.03, ny=6, nt=3))
    c.add(b.patch('flank_stripe', 2.25, -2.25, 4.3, 4.5, M['accent'], off=0.005, ny=24, nt=1))
    for s in (-1, 1):
        c.add(box('step', (s * 0.99, 0.1, 0.36), (0.12, 1.3, 0.04), M['grey'], 0.01))
    c.build({'rim': 'ten'})
    return 'D04'


def build_d05():
    """Sprint (AWD modern turbo hot hatch, D-081): inspired by current homologation hatches - low sharp
    nose, slim angry lamps, big low grille, sloping coupe roof over wide rear hips, black two-tone
    roof, full-width tail light bar, roof scoop and LED day lights fitted from the factory."""
    reset()
    M = palette((0.56, 0.58, 0.6), (0.9, 0.08, 0.08), (0.16, 0.16, 0.18), caliper=(0.9, 0.08, 0.08), suit=(0.85, 0.1, 0.1))
    c = start('D05', [
        (2.00, 0.80, 0.22, 0.60, 0.66, 0.72, 'bonnet'),
        (1.92, 0.87, 0.20, 0.72, 0.76, 0.78, 'bonnet'),
        (1.60, 0.90, 0.19, 0.80, 0.84, 0.80, 'bonnet'),
        (1.10, 0.905, 0.19, 0.85, 0.89, 0.81, 'bonnet'),
        (0.70, 0.91, 0.19, 0.87, 0.92, 0.80, 'ws'),
        (0.00, 0.90, 0.19, 0.89, 1.36, 0.64, 'roof'),
        (-0.70, 0.93, 0.19, 0.91, 1.33, 0.62, 'rw'),
        (-1.45, 0.95, 0.20, 0.93, 1.02, 0.80, 'deck'),
        (-1.85, 0.94, 0.21, 0.94, 1.00, 0.80, 'deck'),
        (-1.96, 0.91, 0.23, 0.92, 0.98, 0.78, 'deck'),
        (-2.00, 0.87, 0.26, 0.89, 0.95, 0.74, None),
    ], M, (2.56, 1.70, 0.35, 0.23))
    b = c.b
    c.mod_stock = {'roof': 'scoop', 'lights': 'drl'}
    c.glasshouse(b_pillars=[-0.45])
    c.doors([0.68, -0.6], [-0.48])
    c.panel_lines(bonnet_y=0.78, boot_y=-1.5, fuel_y=-1.2)
    c.mirrors()
    c.interior(rear_seats=True)
    c.driver()
    c.arch_lips()
    c.realism(rub_strip=False)
    c.real_details(hatch=True, drive='AWD')
    c.face_lamps(0.56, 0.6, 0.34, 0.07, front=True, style='single')
    ty = max(c.fy(-0.7, 0.84, False), c.fy(0.7, 0.84, False))
    c.add(box('tail_panel', (0, ty - 0.008, 0.84), (1.5, 0.02, 0.1), M['trim'], 0.01))
    c.add(box('tail_bar', (0, ty - 0.02, 0.84), (1.42, 0.012, 0.035), M['tail']))
    for s in (-1, 1):
        c.add(box('tail_lamp', (s * 0.6, ty - 0.022, 0.84), (0.22, 0.012, 0.075), M['tail']))
        c.add(box('tail_ind', (s * 0.6, ty - 0.026, 0.815), (0.12, 0.01, 0.02), M['amber_l' if s < 0 else 'amber_r']))
        c.add(box('tail_rev', (s * 0.3, ty - 0.026, 0.84), (0.08, 0.01, 0.025), M['reverse']))
    c.grille(0.4, 0.86, 0.17, style='honey')
    c.intake(0.68, 0.34, 0.16, 0.12)
    c.plate(c.front, 0.235, True)
    c.plate(c.rear, 0.62, False)
    c.exhausts({'dual': [(-0.62, 0.05), (0.62, 0.05)], 'center': [(-0.08, 0.048), (0.08, 0.048)], 'quad': [(0.5, 0.042), (0.62, 0.042), (-0.5, 0.042), (-0.62, 0.042)]}, 'dual')
    c.spoilers('lip', 1.33, -0.74, 1.2)
    c.aero('lip', 'aero', 'diffuser')
    c.weapons()
    c.hoods('vent', 1.55, 1.15)
    c.add(b.patch('roof_black', c.y_roof0 - 0.02, c.y_rw0 + 0.02, 7.95, 10.0, M['trim'], off=0.004, ny=10, nt=4))  # two-tone roof
    c.add(b.patch('side_stripe', 1.7, -1.7, 2.55, 2.7, M['accent'], off=0.005, ny=24, nt=1))
    c.build({'rim': 'y_spoke'})
    return 'D05'


def review(path, stock_only=True):
    sc = bpy.context.scene
    if stock_only:  # hide non-stock variants in the review render, as the game does
        data = json.loads((CUSTOM / (path.stem.split('_')[0] + '.json')).read_text(encoding='utf-8'))
        keep = {f"PART_{s}_{v['stock']}" for s, v in data['slots'].items() if s != 'rims'}
        rim_stock = data['slots']['rims']['stock']
        for o in sc.objects:
            if (o.name.startswith('PART_') and o.name not in keep) or (o.name.startswith('RIM_') and not o.name.startswith(f'RIM_{rim_stock}_')):
                o.hide_render = True
                o['_hidden_by_default'] = True
    world = bpy.data.worlds.new('w'); sc.world = world; world.use_nodes = True
    world.node_tree.nodes['Background'].inputs[0].default_value = (0.55, 0.6, 0.7, 1)
    bpy.ops.object.light_add(type='SUN'); bpy.context.object.data.energy = 4; bpy.context.object.rotation_euler = (0.7, 0.3, 0.8)
    bpy.ops.mesh.primitive_plane_add(size=30)
    bpy.context.object.data.materials.append(mat('floor', (0.5, 0.52, 0.56)))
    views = [('', (-5.6, 4.6, 2.0), (0, 0.3, 0.6)), ('_rear', (4.8, -5.2, 1.9), (0, -0.2, 0.7))]
    eye = bpy.data.objects.get('DRIVER_EYE')
    if eye:
        e = eye.matrix_world.translation.copy()
        views.append(('_cockpit', tuple(e), tuple(e + Vector((0, 3.0, -0.55)))))
        views.append(('_window', tuple(e + Vector((2.2, 0.35, -0.1))), tuple(e + Vector((0, 0.35, -0.35)))))
        # the driver's own model sheets: tools/generate_driver.py (D-076)
        views.append(('_console', tuple(e + Vector((0.2, 0.1, 0.05))), tuple(e + Vector((0.3, 0.6, -0.5)))))
    for name, loc, aim in views:
        for hidden in ('MESH_SHIFT_HAND', 'MESH_TAUNT_HAND'):  # the game shows these only when used
            if bpy.data.objects.get(hidden):
                bpy.data.objects[hidden].hide_render = True
        solo = name.rstrip('12') in ('_portrait', '_profile')  # the character alone, like a model sheet
        hv = int(name[-1]) if name and name[-1] in '12' else 0
        for k, hn in enumerate(('MESH_DRIVER_HEAD', 'MESH_DRIVER_HEAD_1', 'MESH_DRIVER_HEAD_2')):
            if bpy.data.objects.get(hn):
                bpy.data.objects[hn].hide_render = k != hv or name == '_cockpit'
        for o in bpy.context.scene.objects:
            if o.type == 'MESH' and not o.name.startswith('MESH_DRIVER_HEAD') and o.name not in ('MESH_DRIVER_TORSO', 'MESH_UPPER_ARM_L', 'MESH_UPPER_ARM_R', 'MESH_FOREARM_L', 'MESH_FOREARM_R') and not o.name.startswith('Plane'):
                o.hide_render = solo or o.get('_hidden_by_default', False) or (o.name in ('MESH_SHIFT_HAND', 'MESH_TAUNT_HAND', 'MESH_POINT_HAND_L', 'MESH_POINT_HAND_R'))
        bpy.ops.object.camera_add(location=loc); cam = bpy.context.object
        cam.rotation_euler = (Vector(aim) - cam.location).to_track_quat('-Z', 'Y').to_euler(); sc.camera = cam
        sc.render.engine = 'BLENDER_EEVEE_NEXT'; sc.render.resolution_x = 1100; sc.render.resolution_y = 680
        cam.data.lens = {'_cockpit': 18, '_face': 35, '_driver': 22, '_console': 20, '_portrait': 60, '_profile': 60}.get(name.rstrip('12'), 50)
        cam.data.clip_start = 0.02
        sc.render.filepath = str(path).replace('.png', name + '.png'); bpy.ops.render.render(write_still=True)


def mods_review(path):
    """D-080: the modern mod kit on one car (widebody, roof, light signature, rims), two angles."""
    show = {'PART_flares_widebody', 'PART_lights_bar', 'PART_spoiler_wing', 'PART_aero_f_splitter'}
    roof = {'D01': 'scoop', 'D02': 'fin', 'D03': 'rack', 'D04': 'rack', 'D05': 'fin'}.get(path.stem.split('_')[0], 'scoop')
    rim_v = {'D01': 'y_spoke', 'D02': 'split', 'D03': 'aero', 'D04': 'y_spoke', 'D05': 'split'}.get(path.stem.split('_')[0], 'y_spoke')
    show.add('PART_roof_' + roof)
    sc = bpy.context.scene
    for o in sc.objects:
        if o.type != 'MESH':
            continue
        if o.name.startswith('PART_'):
            slot = o.name.split('_')[1]
            keep = o.name in show or (o.get('_hidden_by_default') is None and not any(n.startswith('PART_' + slot + '_') for n in show))
            o.hide_render = not keep
        elif o.name.startswith('RIM_'):
            o.hide_render = not o.name.startswith(f'RIM_{rim_v}_')
    for name, loc, aim in (('', (-4.6, 4.2, 1.5), (0, 0.4, 0.6)), ('_rear', (4.2, -4.4, 2.2), (0, -0.2, 0.7))):
        bpy.ops.object.camera_add(location=loc); cam = bpy.context.object
        cam.rotation_euler = (Vector(aim) - cam.location).to_track_quat('-Z', 'Y').to_euler(); sc.camera = cam
        cam.data.lens = 40
        sc.render.filepath = str(path).replace('.png', name + '.png'); bpy.ops.render.render(write_still=True)


car_cabin.setup(dict(bpy=bpy, box=box, cyl=cyl, torus=torus, tube_along=tube_along, link=link, fix_normals=fix_normals, mat=mat, mirror_quad=mirror_quad))
BUILDERS = {'D01': build_d01, 'D02': build_d02, 'D03': build_d03, 'D04': build_d04, 'D05': build_d05}

# ---------------------------------------------------------------- roster cars from data (D-089)

def round_lamps(c, x, z, r, count, front=True, m=None):
    """Classic round lamps on the nose/tail face: chrome ring, lit disc, glass; count per side."""
    M = c.M
    d = 1 if front else -1
    for s in (-1, 1):
        for k in range(count):
            cx = s * (x + (k - (count - 1) / 2) * r * 2.3 * (1 if count > 1 else 0))
            y = c.fy(cx, z, front)
            c.add(torus('lamp_ring', (cx, y + d * 0.012, z), r * 1.08, r * 0.14, M['chrome'], rot=(math.pi / 2, 0, 0), seg=20, mseg=4))
            c.add(cyl('lamp_round', (cx, y + d * 0.008, z), r, 0.012, m or M['lamp'], rot=(math.pi / 2, 0, 0), verts=20))
            c.add(cyl('lamp_glass', (cx, y + d * 0.016, z), r * 0.96, 0.004, M['lens'], rot=(math.pi / 2, 0, 0), verts=20))
    if front:
        c.head_spec = (x, z, r * 2, r * 2)


def tail_bar(c, z, hgt, slats=False):
    """A full-width tail light bar across the tail face (D02/D05 style), optional black slats over it."""
    M = c.M
    w = c.b.st[-1][1] * 1.6
    ty = max(c.fy(-w * 0.45, z, False), c.fy(w * 0.45, z, False))
    c.add(box('tail_panel', (0, ty - 0.008, z), (w + 0.06, 0.02, hgt + 0.04), M['trim'], 0.01))
    c.add(box('tail_bar', (0, ty - 0.02, z), (w, 0.012, hgt), M['tail']))
    for s in (-1, 1):
        c.add(box('tail_ind', (s * w * 0.44, ty - 0.026, z), (w * 0.08, 0.01, hgt), M['amber_l' if s < 0 else 'amber_r']))
        c.add(box('tail_rev', (s * w * 0.3, ty - 0.026, z), (w * 0.07, 0.01, hgt), M['reverse']))
    if slats:
        for k in range(5):
            c.add(box('tail_slat', (0, ty - 0.03, z - hgt / 2 + hgt * (k + 0.5) / 5), (w, 0.01, hgt * 0.07), M['trim']))


def build_design(d):
    """One roster car from its design row (tools/car_designs.py)."""
    reset()
    M = palette(d['paint'], d['accent'], d['rim_rgb'], suit=d['accent'])
    if d.get('metal'):
        p_ = next(n for n in M['paint'].node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
        p_.inputs['Metallic'].default_value = d['metal']
    st = car_designs.stations(d)
    W, zs, nose = d['W'], d['zs'], d['nose']
    c = start(d['id'], st, M, (d['wb'], d['track'], d['r'], d['rw']), bumper_m=M['chrome'] if d.get('bumper') == 'chrome' else None)
    b = c.b
    c.cabin_style = car_designs.CABIN[d['arch']]
    c.mod_stock = {k: d[v] for k, v in (('roof', 'roof_s'), ('lights', 'lights_s')) if d.get(v)}
    four_door = d['arch'] in ('sedan', 'wagon', 'suv', 'van', 'rally')
    mid_roof = (c.y_roof0 + c.y_rw0) / 2
    c.glasshouse(b_pillars=[mid_roof] if four_door else [])
    door_front = c.y_ws0 - 0.05
    if four_door:
        c.doors([door_front, mid_roof + 0.02, mid_roof - 0.8], [mid_roof + 0.15, mid_roof - 0.62])
    else:
        c.doors([door_front, door_front - 1.25], [door_front - 1.1])
    tail = d['tail']
    boot = None if tail in ('hatch', 'wagon', 'bed', 'mid') else c.y_rw1 - 0.04
    c.panel_lines(bonnet_y=c.y_ws0 + 0.04, boot_y=boot, fuel_y=-d['wb'] / 2 + 0.25)
    c.mirrors()
    c.interior(rear_seats=d['arch'] not in ('mid', 'pickup'))
    c.driver()
    c.arch_lips()
    c.realism(rub_strip=d['arch'] in ('sedan', 'wagon', 'van', 'suv'))
    c.real_details(hatch=tail in ('hatch', 'wagon'), drive=d['drive'])
    # --- face: lamps, grille, intakes
    face_z = lambda f: zs + (nose - zs) * f
    hs, hx, hz, hw, hh = d['head']
    if hs in ('quad', 'single', 'slim'):
        c.face_lamps(W * hx, face_z(hz), W * hw, hh, front=True, style='quad' if hs == 'quad' else 'single')
    elif hs == 'round':
        round_lamps(c, W * hx, face_z(hz), hh / 2, 1)
    elif hs == 'twin_round':
        round_lamps(c, W * hx, face_z(hz), hh / 2, 2)
    elif hs == 'popup':  # closed lids on the bonnet edge, slim lamps low in the bumper
        c.face_lamps(W * hx, face_z(hz), W * hw, hh, front=True, style='single')
        for y in (c.front - 0.12, c.front - 0.5):
            c.add(b.line_across('popup_cut', y, 7.05, 8.95, M['line']))
        c.add(b.line_along('popup_side', c.front - 0.12, c.front - 0.5, 7.05, M['line']))
        c.add(b.line_along('popup_side', c.front - 0.12, c.front - 0.5, 8.95, M['line']))
    elif hs == 'hidden':  # behind the grille: only the bezel line shows; the DRL slot uses the grille band
        c.head_spec = (W * 0.6, face_z(hz), W * 0.3, hh)
    ts, tx, tz, tw, th = d['tail_l']
    rear_z = lambda f: st[-1][2] + (st[-1][4] - st[-1][2]) * f
    if ts in ('block', 'vertical'):
        c.face_lamps(W * tx, rear_z(tz), W * tw, th, front=False)
    elif ts in ('bar', 'slim_tail', 'slats_tail'):
        tail_bar(c, rear_z(tz), th, slats=ts == 'slats_tail')
    elif ts == 'round':
        round_lamps(c, W * tx, rear_z(tz), th / 2, 2, front=False, m=M['tail'])
    g = d.get('grille')
    if g:
        gs, gw, gh = g
        gz = face_z(0.62 if hs != 'hidden' else hz)
        if gs == 'twin_kidney':
            for s in (-1, 1):
                y = c.fy(s * W * gw * 0.6, gz)
                c.add(box('kidney', (s * W * gw * 0.6, y + 0.01, gz), (W * gw, 0.02, gh), M['chrome'], 0.02))
                c.add(box('kidney_back', (s * W * gw * 0.6, y + 0.016, gz), (W * gw - 0.03, 0.012, gh - 0.03), M['well']))
        elif gs == 'horseshoe':
            c.grille(gz - 0.03, W * gw, gh, style='honey')
        else:
            c.grille(gz, min(W * gw, W * 1.7), gh, style='slats' if gs in ('slats', 'chrome', 'full') else 'honey')
    if d.get('bumper') != 'chrome':
        c.intake(0, zs + 0.12, W * 0.8, 0.09)
        if d['cls'] in ('B', 'A') or d['arch'] == 'rally':
            c.intake(W * 0.7, zs + 0.14, W * 0.22, 0.1, fog=d['arch'] == 'rally')
    c.plate(c.front, zs + 0.24, True)
    c.plate(c.rear, rear_z(0.45), False)
    # --- mods: exhausts (stock + alternatives), spoilers, aero, weapons, bonnets
    lay = {'single': [(W * 0.55, 0.045)], 'dual': [(-W * 0.6, 0.05), (W * 0.6, 0.05)],
           'quad': [(W * 0.5, 0.042), (W * 0.64, 0.042), (-W * 0.5, 0.042), (-W * 0.64, 0.042)],
           'center': [(-0.08, 0.05), (0.08, 0.05)], 'side': [(W * 0.86, 0.065)], 'none': []}
    stock_ex = d.get('exhaust', 'single')
    alts = {stock_ex} | ({'dual', 'quad'} if stock_ex != 'none' else set())
    c.exhausts({k: lay[k] for k in alts}, stock_ex)
    roof_tail = tail in ('hatch', 'wagon')
    c.spoilers(d.get('spoiler', 'lip'), st[-1][4], c.y_rw0 if roof_tail else c.rear + 0.25, 2 * W * 0.72)
    c.aero(d.get('aero_f', 'none'), 'none', 'none')
    c.weapons()
    c.hoods(d.get('hood_s', 'stock'), c.front - 0.45, c.y_ws0 + 0.35)
    # --- paint scheme
    sp = d.get('stripe', 'none')
    A = M['accent']
    if sp == 'center':
        for y0, y1 in ((c.front, c.y_ws0 + 0.02), (c.y_roof0 - 0.02, c.y_rw0 + 0.02), (c.y_rw1 - 0.02, c.rear)):
            c.add(b.patch('stripe', y0, y1, 9.6, 10.0, A, off=0.005, ny=10, nt=1))
    elif sp == 'twin':
        for y0, y1 in ((c.front, c.y_ws0 + 0.02), (c.y_roof0 - 0.02, c.y_rw0 + 0.02), (c.y_rw1 - 0.02, c.rear)):
            c.add(b.patch('stripe', y0, y1, 9.25, 9.6, A, off=0.005, ny=10, nt=1))
    elif sp == 'side':
        c.add(b.patch('stripe', c.front - 0.1, c.rear + 0.1, 3.3, 3.55, A, off=0.005, ny=24, nt=1))
    elif sp == 'rally':
        c.add(b.patch('side_decal', c.y_ws0, c.y_rw1, 2.6, 4.2, A, off=0.005, ny=16, nt=3))
    elif sp == 'checker':
        n = int((c.front - c.rear - 0.4) / 0.3)
        for k in range(n):
            y0 = c.front - 0.2 - k * 0.3
            c.add(b.patch('checker', y0, y0 - 0.15, 4.0 + (0.18 if k % 2 else 0), 4.18 + (0.18 if k % 2 else 0), A, off=0.005, ny=2, nt=1))
    elif sp == 'panda':
        c.add(b.patch('panda', c.front, c.rear, 0.0, 3.6, A, off=0.004, ny=24, nt=4))
    elif sp == 'tricolor':
        for k, t0 in enumerate((3.2, 3.42, 3.64)):
            c.add(b.patch('stripe', c.front - 0.2, c.rear + 0.2, t0, t0 + 0.12, A if k != 1 else M['tail'], off=0.005, ny=24, nt=1))
    elif sp == 'tail_band':
        c.add(b.patch('tail_band', c.rear + 0.75, c.rear + 0.45, 3.0, 10.0, A, off=0.005, ny=3, nt=6))
    elif sp == 'strakes':
        for k in range(5):
            c.add(b.line_along('strake', c.y_ws0 - 0.4, -d['wb'] / 2 + d['r'] + 0.1, 3.2 + 0.28 * k, M['trim'], 0.016))
    elif sp == 'two_tone':
        c.add(b.patch('two_tone', c.front, c.y_ws0 + 0.1, 0.0, 10.0, A, off=0.004, ny=10, nt=8))
    if d.get('roof_color'):
        c.add(b.patch('roof_paint', c.y_roof0 - 0.02, c.y_rw0 + 0.02, 8.0, 10.0, mat('toon_roof_paint', d['roof_color'], 0.3), off=0.004, ny=8, nt=4))
    # --- extras
    ym = mid_roof
    zr = c.top_z(0, ym)
    if d.get('taxi_sign'):
        c.add(box('taxi_sign', (0, ym, zr + 0.11), (0.6, 0.2, 0.2), M['lamp'], 0.03))
        c.add(box('taxi_base', (0, ym, zr + 0.015), (0.66, 0.24, 0.03), M['trim'], 0.01))
    if d.get('ice_cream'):
        bpy.ops.mesh.primitive_cone_add(vertices=16, radius1=0.02, radius2=0.22, depth=0.55, location=(0, ym, zr + 0.3))
        o = bpy.context.object
        o.data.materials.append(M['accent'])
        c.add(o)
        c.add(cyl('scoop', (0, ym, zr + 0.62), 0.24, 0.2, mat('toon_scoop', (0.98, 0.75, 0.85), 0.5), rot=(0, 0, 0), verts=16))
    if d.get('spare'):
        ty = c.fy(0, rear_z(0.5), False)
        c.add(cyl('spare', (0, ty - 0.12, rear_z(0.5)), d['r'] * 0.95, 0.2, M['tyre'], rot=(math.pi / 2, 0, 0), verts=24))
        c.add(cyl('spare_cover', (0, ty - 0.225, rear_z(0.5)), d['r'] * 0.7, 0.01, M['accent'], rot=(math.pi / 2, 0, 0), verts=24))
    if d.get('bull_bar'):
        fy_ = c.fy(0, zs + 0.3) + 0.12
        pts = [Vector((-W * 0.7, fy_ - 0.05, zs + 0.1)), Vector((-W * 0.7, fy_, nose - 0.05)), Vector((W * 0.7, fy_, nose - 0.05)), Vector((W * 0.7, fy_ - 0.05, zs + 0.1))]
        c.add(tube_along('bull_bar', pts, 0.035, M['dark'], sides=10))
        c.add(tube_along('bull_bar_x', [Vector((-W * 0.7, fy_, zs + 0.35)), Vector((W * 0.7, fy_, zs + 0.35))], 0.03, M['dark'], sides=10))
    c.build({'rim': d['rim']})
    return d['id']


for _d in car_designs.DESIGNS:
    BUILDERS[_d['id']] = (lambda dd: (lambda: build_design(dd)))(_d)
wanted = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else list(BUILDERS)
for cid in wanted:
    car = BUILDERS[cid]()
    d = OUT / car
    d.mkdir(parents=True, exist_ok=True)
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.export_scene.gltf(filepath=str(d / f'{car}.glb'), export_format='GLB', use_selection=True, export_apply=True, export_yup=True,
                              export_vertex_color='ACTIVE')  # the drivers' face tones (D-068)
    faces = {o.name: len(o.data.polygons) for o in bpy.context.scene.objects if o.type == 'MESH'}
    body = faces.get('BODY', 0)
    total = sum(faces.values())
    review(d / f'{car}_review.png')
    mods_review(d / f'{car}_mods.png')
    print('TUNER_COMPLETE', car, 'body_faces', body, 'all_faces', total, flush=True)
