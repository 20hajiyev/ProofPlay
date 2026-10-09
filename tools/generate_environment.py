"""Original Harbor kit and Dock Loop. Run with Blender --background --python this_file."""
import bpy, math, json, hashlib, random
from pathlib import Path
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
ENV = ROOT / 'assets/environment/harbor'
TRACK = ROOT / 'assets/tracks/dock_loop'
ENV.mkdir(parents=True, exist_ok=True)
TRACK.mkdir(parents=True, exist_ok=True)
random.seed(7429)
bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
for d in list(bpy.data.materials): bpy.data.materials.remove(d)

def mat(name, color, metallic=0, rough=.65, emission=0):
    m=bpy.data.materials.new(name); m.diffuse_color=(*color,1); m.use_nodes=True
    p=m.node_tree.nodes.get('Principled BSDF'); p.inputs['Base Color'].default_value=(*color,1)
    p.inputs['Metallic'].default_value=metallic; p.inputs['Roughness'].default_value=rough
    if emission: p.inputs['Emission Color'].default_value=(*color,1); p.inputs['Emission Strength'].default_value=emission
    return m
# Baked surface materials (tools/generate_harbor_materials.py, plan 3.3 contract): albedo sRGB,
# normal OpenGL, ORM (G roughness, B metallic). The glTF exporter turns this node pattern into
# baseColorTexture + metallicRoughnessTexture + normalTexture.
MATS=ROOT/'assets/materials/harbor'
SCAN=ROOT/'assets/materials/polyhaven'; SCANNED=ROOT/'assets/materials/scanned'
# CC0 Poly Haven scans (assets/materials/polyhaven/LICENSE.md) where a real surface reads far
# better than synthesis; kind -> file. Procedural maps remain for everything else.
SCAN_FILES={
 'asphalt_01':{'albedo':SCANNED/'asphalt_graded_diff.jpg','orm':SCAN/'asphalt_01_arm_2k.jpg','normal':SCAN/'asphalt_01_nor_gl_2k.jpg'},
 'concrete_floor_02':{'albedo':SCANNED/'concrete_graded_diff.jpg','orm':SCAN/'concrete_floor_02_arm_2k.jpg','normal':SCAN/'concrete_floor_02_nor_gl_2k.jpg'},
 'rusty_metal_02':{'albedo':SCAN/'rusty_metal_02_diff_2k.jpg','orm':SCAN/'rusty_metal_02_arm_2k.jpg','normal':SCAN/'rusty_metal_02_nor_gl_2k.jpg'},
 **{f'container_{c}':{'albedo':SCANNED/f'container_{c}_diff.jpg','orm':SCANNED/'container_arm.jpg','normal':SCANNED/'container_nor_gl.jpg'} for c in ['teal','red','blue','orange','grey']}}
def tex_mat(name, base):
    m=bpy.data.materials.new(name); m.use_nodes=True; nt=m.node_tree; p=nt.nodes.get('Principled BSDF')
    def img(kind, color):
        path=SCAN_FILES[base][kind] if base in SCAN_FILES else MATS/f'{base}_{kind}.png'
        n=nt.nodes.new('ShaderNodeTexImage'); n.image=bpy.data.images.load(str(path), check_existing=True)
        n.image.colorspace_settings.name=color; return n
    a=img('albedo','sRGB'); nt.links.new(a.outputs['Color'],p.inputs['Base Color'])
    o=img('orm','Non-Color'); sep=nt.nodes.new('ShaderNodeSeparateColor'); nt.links.new(o.outputs['Color'],sep.inputs['Color'])
    nt.links.new(sep.outputs['Green'],p.inputs['Roughness']); nt.links.new(sep.outputs['Blue'],p.inputs['Metallic'])
    nm=img('normal','Non-Color'); nmap=nt.nodes.new('ShaderNodeNormalMap'); nt.links.new(nm.outputs['Color'],nmap.inputs['Color']); nt.links.new(nmap.outputs['Normal'],p.inputs['Normal'])
    return m
M={
 'teal':tex_mat('Harbor Petrol','paint_teal'), 'orange':tex_mat('Signal Orange','paint_orange'),
 'red':tex_mat('Oxide Red','paint_red'), 'blue':tex_mat('Harbor Blue','paint_blue'), 'grey':tex_mat('Primer Grey','paint_grey'),
 'steel':tex_mat('Galvanized Steel','galvanized_steel'), 'dark':mat('Charcoal',(.025,.038,.05)),
 'wood':mat('Pallet Pine',(.43,.23,.09)), 'concrete':tex_mat('Cast Concrete','barrier_concrete'),
 'quay':tex_mat('Quay Concrete','concrete_floor_02'), 'rust':tex_mat('Weathered Steel','rusty_metal_02'),
 **{'c_'+k:tex_mat('Container '+k.title(),'container_'+k) for k in ['teal','red','blue','orange','grey']},
 'white':mat('Paint Ivory',(.86,.87,.78)), 'yellow':mat('Safety Ochre',(.98,.62,.04)),
 'glow':mat('Pickup Cyan',(.02,.75,1),.2,.3,3), 'light':mat('Lamp Warm',(1,.65,.3),0,.4,4),
 'asphalt':tex_mat('Asphalt','asphalt_01'), 'water':tex_mat('Harbor Water','water')
}
# Toon art direction (D-050, the default): flat, saturated colours, no textures - the game draws
# them with the cartoon shader and outlines. RACER_ENV_STYLE=real keeps the scanned PBR look.
import os
if os.environ.get('RACER_ENV_STYLE', 'toon') == 'toon':
    for _k, (_name, _rgb, _rough) in {
        'teal': ('Toon Teal', (.05, .42, .45), .6), 'orange': ('Toon Orange', (.95, .42, .08), .6),
        'red': ('Toon Red', (.75, .12, .08), .6), 'blue': ('Toon Blue', (.12, .28, .7), .6), 'grey': ('Toon Grey', (.45, .47, .5), .6),
        'steel': ('Toon Steel', (.55, .58, .62), .5), 'concrete': ('Toon Barrier', (.7, .72, .74), .8),
        'quay': ('Toon Quay', (.42, .34, .24), .9), 'rust': ('Toon Crane', (.9, .45, .12), .6),
        'c_teal': ('Toon Box Teal', (.08, .55, .55), .6), 'c_red': ('Toon Box Red', (.85, .2, .15), .6),
        'c_blue': ('Toon Box Blue', (.15, .35, .8), .6), 'c_orange': ('Toon Box Orange', (.98, .55, .12), .6),
        'c_grey': ('Toon Box Grey', (.6, .62, .66), .6),
        'asphalt': ('Toon Road', (.07, .08, .11), .85), 'water': ('Toon Water', (.1, .55, .7), .2),
        'white': ('Toon Paint White', (.96, .96, .92), .6), 'yellow': ('Toon Yellow', (1.0, .78, .1), .6),
    }.items():
        M[_k] = mat(_name, _rgb, 0, _rough)
    # Hand-painted toon textures on the big surfaces (D-077, tools/generate_toon_materials.py): the
    # same objects, a comic-book finish - posterized tones, ink cracks and seams, grime and wear.
    def toon_tex(name, png):
        m = bpy.data.materials.new(name); m.use_nodes = True; nt = m.node_tree
        p = next(n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED')
        t = nt.nodes.new('ShaderNodeTexImage')
        t.image = bpy.data.images.load(str(ROOT / 'assets/materials/toon' / png), check_existing=True)
        nt.links.new(t.outputs['Color'], p.inputs['Base Color'])
        p.inputs['Roughness'].default_value = .85
        return m
    M.update({'asphalt': toon_tex('Toon Road', 'toon_asphalt.png'), 'concrete': toon_tex('Toon Barrier', 'toon_barrier.png'),
              'quay': toon_tex('Toon Quay', 'toon_quay.png'), 'water': toon_tex('Toon Water', 'toon_water.png')})
    TILE_TOON = {'asphalt': 6.0, 'quay': 16.0, 'apron': 6.0}  # painted maps: road 6 m, quay 16 m (D-086)
    M['apron'] = toon_tex('Road Apron', 'toon_apron.png')  # worn asphalt outside the barriers; "Road": no ink
    # Weathered paint on containers, sheds and cranes (D-082): rain streaks, chips with rust runs,
    # riveted plate seams on the crane girders. Same names, so the game's ink rules still apply.
    M.update({'c_' + k: toon_tex('Toon Box ' + k.title(), f'toon_box_{k}.png') for k in ['teal', 'red', 'blue', 'orange', 'grey']})
    M.update({'teal': toon_tex('Toon Teal', 'toon_clad_teal.png'), 'grey': toon_tex('Toon Grey', 'toon_clad_grey.png'),
              'rust': toon_tex('Toon Crane', 'toon_crane.png'), 'emblem': toon_tex('Toon Emblem', 'toon_emblems.png')})
# Detail pass (D-054) materials, both styles. Ground-level ones carry "Road" in the name: the game
# draws no outline on ground materials (D-050).
M.update({
    'glassd': mat('Toon Window', (.06, .09, .13), 0, .15), 'hull': mat('Toon Hull', (.08, .1, .16), 0, .6),
    'hull_red': mat('Toon Hull Red', (.6, .1, .08), 0, .6), 'boardc': mat('Toon Board Magenta', (.85, .15, .5), 0, .6),
    'kerb': mat('Road Kerb Red', (.8, .1, .08), 0, .7), 'patch': mat('Road Patch', (.1, .11, .14), 0, .9),
    'skid': mat('Road Skid', (.03, .03, .04), 0, 1), 'manhole': mat('Road Manhole', (.22, .22, .24), .3, .6),
    'kerb_white': mat('Road Kerb White', (.95, .95, .92), 0, .7),
})
# Metres per texture repeat: same texel density on every object regardless of its size.
TILE={'rust':2.0,'c_teal':1.9,'c_red':1.9,'c_blue':1.9,'c_orange':1.9,'c_grey':1.9,'teal':2.5,'orange':2.5,'red':2.5,'blue':2.5,'grey':2.5,'steel':1.5,'concrete':2.0,'quay':4.0,'asphalt':3.0,'water':24.0}
if os.environ.get('RACER_ENV_STYLE', 'toon') == 'toon':
    TILE.update(TILE_TOON)
def world_uv(o,tile):
    """Box projection in object space: each face takes the two axes it is not facing."""
    me=o.data; uv=me.uv_layers.active or me.uv_layers.new(name='UVMap')
    for poly in me.polygons:
        ax=max(range(3),key=lambda k:abs(poly.normal[k])); a,b=[k for k in range(3) if k!=ax]
        for li in poly.loop_indices:
            co=me.vertices[me.loops[li].vertex_index].co+o.location
            uv.data[li].uv=(co[a]/tile,co[b]/tile)
def box(name,loc,scale,material,bevel=0):
    bpy.ops.mesh.primitive_cube_add(size=1,location=loc); o=bpy.context.object; o.name=name; o.dimensions=scale
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True); o.data.materials.append(M[material])
    if bevel:
        mod=o.modifiers.new('Manufactured edges','BEVEL'); mod.width=bevel; mod.segments=1
        bpy.context.view_layer.objects.active=o; bpy.ops.object.modifier_apply(modifier=mod.name)
    if material in TILE: world_uv(o,TILE[material])
    return o
def cyl(name,loc,r,depth,material,vertices=12):
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices,radius=r,depth=depth,location=loc)
    o=bpy.context.object; o.name=name; o.data.materials.append(M[material])
    if material in TILE: world_uv(o,TILE[material])
    return o
def beam(name,a,b,width,material):
    d=Vector(b)-Vector(a); o=box(name,(Vector(a)+Vector(b))/2,(width,width,d.length),material)
    o.rotation_euler=d.to_track_quat('Z','Y').to_euler(); return o
def cone(name,loc,r1,r2,depth,material):
    bpy.ops.mesh.primitive_cone_add(vertices=12,radius1=r1,radius2=r2,depth=depth,location=loc)
    o=bpy.context.object; o.name=name; o.data.materials.append(M[material])
    if material in TILE: world_uv(o,TILE[material])
    return o

SHEETS={}
def sheet_mat(material):
    """Copy of a material named '<name> Sheet': the game skips ink on it (sub-pixel ribs speckle)."""
    if material not in SHEETS:
        m=M[material].copy(); m.name=M[material].name+' Sheet'; SHEETS[material]=m
    return SHEETS[material]

def S(key):
    """Material key for the no-ink copy of M[key] (small hardware that would speckle at distance)."""
    k=key+'_s'
    if k not in M: M[k]=sheet_mat(key)
    return k

def corrugated(name,origin,u,n,length,height,material,depth=.035,period=.28,v=(0,0,1)):
    """Trapezoid-profile sheet (real container/warehouse cladding) as one mesh. The slopes are ~30
    degrees - under the ink pass's 37 degree crease threshold (P-003) - so it reads by toon shading
    bands, not by a black line per rib at distance. u: along the profile, n: outward, v: extrusion."""
    u=Vector(u).normalized(); n=Vector(n).normalized(); v=Vector(v).normalized(); o0=Vector(origin)
    prof=[]; x=0.0
    while x<length-1e-6:
        seg=[(0,0),(period*.25,0),(period*.25+.06,depth),(period*.75-.06,depth),(period*.75,0)]
        for dx,dz in seg:
            if x+dx<=length: prof.append((x+dx,dz))
        x+=period
    prof.append((length,0))
    vs=[]; fs=[]
    for px,pz in prof:
        for h in (0,height): vs.append(tuple(o0+u*px+n*pz+v*h))
    for i in range(len(prof)-1): fs.append((i*2,i*2+2,i*2+3,i*2+1))
    me=bpy.data.meshes.new(name); me.from_pydata(vs,[],fs); me.materials.append(sheet_mat(material))
    o=bpy.data.objects.new(name,me); bpy.context.scene.collection.objects.link(o)
    # face outward (+n)
    for p in me.polygons:
        if p.normal.dot(n)<0: p.flip()
    if material in TILE: world_uv(o,TILE[material])  # the painted maps need UVs on the cladding too (D-082)
    return o

def decal(name,center,u,v,w,h,cell,material='emblem'):
    """A flat panel showing one cell of a 2 x 2 atlas (D-082 container emblems); u right, v up."""
    c=Vector(center); u=Vector(u).normalized()*w/2; v=Vector(v).normalized()*h/2
    me=bpy.data.meshes.new(name); me.from_pydata([tuple(c-u-v),tuple(c+u-v),tuple(c+u+v),tuple(c-u+v)],[],[(0,1,2,3)])
    u0=(cell%2)*.5; v0=.5 if cell<2 else 0.0  # image row 0 is the top: cells 0,1 are the upper half
    uv=me.uv_layers.new(name='UVMap')
    for li,(a,b) in enumerate([(u0,v0),(u0+.5,v0),(u0+.5,v0+.5),(u0,v0+.5)]): uv.data[li].uv=(a,b)
    me.materials.append(M[material] if material in M else bpy.data.materials.new(material))
    o=bpy.data.objects.new(name,me); bpy.context.scene.collection.objects.link(o)
    return o

def build(kind):
    if kind.startswith('container'):
        paint=kind.split('_')[1] if '_' in kind else 'teal'
        c='c_'+paint; L,W,H=6.06,2.44,2.59
        # Real ISO box (D-054): corrugated walls, frame rails, corner posts and castings, ribbed
        # doors with four locking bars, cam keepers, handles, hinges and a placard.
        box('container_core',(0,0,H/2),(W-.24,L-.3,H-.34),c)  # well inside the sheets: no coplanar z-fighting
        for sx in [-1,1]:
            corrugated('side_wall',(sx*(W/2-.06),-L/2+.12,.14),(0,1,0),(sx,0,0),L-.24,H-.3,c)
        corrugated('end_wall',(-W/2+.14,L/2-.08,.14),(1,0,0),(0,1,0),W-.28,H-.3,c)
        corrugated('roof',(-W/2+.1,-L/2+.2,H-.1),(0,1,0),(0,0,1),L-.4,W-.2,c,depth=.02,period=.6,v=(1,0,0))
        for sx in [-1,1]:
            for z in [.08,H-.08]: box('side_rail',(sx*(W/2-.05),0,z),(.1,L-.1,.16),c)
            for sy in [-1,1]:
                box('corner_post',(sx*(W/2-.08),sy*(L/2-.08),H/2),(.16,.16,H),c)
                for z in [.09,H-.09]: box('casting',(sx*(W/2-.08),sy*(L/2-.08),z),(.18,.18,.18),'dark')
        for sy in [-1,1]:
            for z in [.1,H-.1]: box('end_rail',(0,sy*(L/2-.05),z),(W,.1,.2),c)
        for x in [-.6,.6]:
            box('door',(x,-L/2+.03,H/2),(1.14,.05,H-.32),c)
            for k in range(4): box('door_rib',(x,-L/2-.005,.45+k*.58),(1.02,.03,.07),S(c))
            for dx in [-.36,.36]:
                box('locking_bar',(x+dx,-L/2-.045,H/2),(.035,.035,H-.26),S('steel'))
                for z in [.2,H-.2]: box('cam_keeper',(x+dx,-L/2-.035,z),(.09,.05,.08),S('steel'))
                box('bar_handle',(x+dx+.07,-L/2-.07,1.15),(.17,.03,.04),S('steel'))
            for z in [.45,1.3,2.15]: box('hinge',(x*1.95,-L/2-.02,z),(.07,.07,.15),S('steel'))
        box('placard',(.92,-L/2-.035,H-.45),(.3,.012,.22),S('white'))
        if 'emblem' in M:  # the shipping line's emblem panel on both long sides (D-082), toon style only
            cell={'teal':1,'red':0,'blue':3,'orange':2,'grey':0}[paint]
            for sx in [-1,1]:
                decal('emblem',(sx*(W/2-.012),.5,H*.6),(0,sx,0),(0,0,1),2.4,1.0,cell)
        box('csc_plate',(-.92,-L/2-.035,.6),(.22,.012,.14),S('steel'))
        for k in range(4): box('id_code',(-.9+k*.13,-L/2-.035,H-.3),(.09,.012,.12),S('white'))
    elif kind=='bollard':
        cyl('base',(0,0,.065),.32,.13,'steel'); cyl('post',(0,0,.55),.18,.95,'yellow'); box('cap',(0,0,1.02),(.55,.3,.16),'yellow',.03)
    elif kind=='pallet':
        for y in [-.44,0,.44]: box('runner',(0,y,.075),(1.2,.14,.15),'wood')
        for x in [-.5,-.25,0,.25,.5]: box('slat',(x,0,.18),(.18,1,.06),'wood')
    elif kind=='crate':
        box('core',(0,0,.6),(1.15,1.15,1.2),'wood',.015)
        for z in [.1,1.1]:
            for x in [-.59,.59]: box('strap',(x,0,z),(.06,1.25,.12),'steel')
            for y in [-.59,.59]: box('strap',(0,y,z),(1.25,.06,.12),'steel')
    elif kind=='concrete_barrier':
        box('foot',(0,0,.15),(3,.7,.3),'concrete',.08); box('wall',(0,0,.61),(3,.38,.92),'concrete',.06)
        for x in [-1.1,-.55,0,.55,1.1]: box('reflector',(x,-.202,.88),(.25,.015,.14),'yellow')
    elif kind=='guardrail':
        for x in [-1.6,1.6]: box('post',(x,0,.6),(.12,.14,1.2),'steel')
        for z in [.58,.82]: box('rail',(0,-.1,z),(4,.12,.22),'steel',.035)
    elif kind=='cone':
        box('rubber_base',(0,0,.045),(.5,.5,.09),'dark',.025); cone('cone',(0,0,.4),.2,.035,.65,'orange')
        cone('reflective_band',(0,0,.48),.127,.094,.13,'white')
    elif kind=='lamp':
        box('foot',(0,0,.12),(.6,.6,.24),'concrete'); cyl('mast',(0,0,4),.085,8,'steel')
        beam('arm',(0,0,7.9),(0,-1.2,8),.1,'steel'); box('housing',(0,-1.15,7.95),(.48,.9,.16),'dark'); box('lens',(0,-1.15,7.86),(.4,.75,.035),'light')
    elif kind=='chevron_sign':
        for x in [-.6,.6]: box('post',(x,0,.9),(.07,.07,1.8),'steel')
        box('board',(0,0,1.65),(2,.08,.85),'dark',.025)
        for x in [-.55,.1,.75]:
            beam('chevron',(x-.25,-.051,1.95),(x+.08,-.051,1.65),.11,'yellow')
            beam('chevron',(x+.08,-.051,1.65),(x-.25,-.051,1.35),.11,'yellow')
    elif kind=='pickup_pad':
        cyl('base',(0,0,.08),1.15,.16,'dark',24); cyl('energy_ring',(0,0,.17),.94,.04,'glow',24); cyl('inner',(0,0,.198),.73,.025,'steel',24)
    elif kind=='start_gantry':
        for x in [-8,8]:
            box('foot',(x,0,.3),(1.3,2,.6),'concrete')
            for y in [-.4,.4]: box('leg',(x,y,3.25),(.24,.24,6.5),'teal')
        for z in [5.7,6.6]: beam('span',(-8,0,z),(8,0,z),.22,'teal')
        box('header',(0,-.06,6.15),(9,.28,.75),'dark')
        for x in [-2,-1,0,1,2]: cyl('start_light',(x,-.25,6.15),.18,.12,'glow')
    elif kind=='warehouse':
        # Port shed (D-054): corrugated cladding, plinth, three roller doors with frames, dock
        # bumpers and lamps, clerestory windows, gutters and downpipes, roof sheets, skylights,
        # vents and AC units, a personnel door with canopy, a company board (fictional).
        WX,WY,WH=24,16,8
        box('plinth',(0,0,.4),(WX+.1,WY+.1,.8),'concrete')
        box('core',(0,0,WH/2),(WX-.2,WY-.2,WH),'teal')
        corrugated('wall_front',(-WX/2,-WY/2,.8),(1,0,0),(0,-1,0),WX,WH-.9,'teal',depth=.05,period=.4)
        corrugated('wall_back',(-WX/2,WY/2,.8),(1,0,0),(0,1,0),WX,WH-.9,'teal',depth=.05,period=.4)
        for sx in [-1,1]: corrugated('wall_side',(sx*WX/2,-WY/2,.8),(0,1,0),(sx,0,0),WY,WH-.9,'teal',depth=.05,period=.4)
        for sx in [-1,1]:
            for sy in [-1,1]: box('corner_trim',(sx*WX/2,sy*WY/2,WH/2),(.3,.3,WH),'steel')
        box('fascia',(0,0,WH+.2),(WX+.3,WY+.3,.4),'steel')
        corrugated('roof_sheet',(-WX/2+.2,-WY/2+.2,WH+.4),(1,0,0),(0,0,1),WX-.4,WY-.4,'grey',depth=.04,period=.5,v=(0,1,0))
        for x in [-7,0,7]: box('skylight',(x,0,WH+.5),(2.2,WY-3,.12),'glassd')
        for x in [-9,9]:
            box('ac_unit',(x,5,WH+.9),(2,1.2,1),'white',.05)
            cyl('ac_fan',(x,5,WH+1.42),.4,.05,'dark',16)
        for x in [-4,4]: cyl('roof_vent',(x,-5,WH+1.1),.35,1.4,'steel',12)
        for x in [-8,0,8]:
            box('door_frame',(x,-WY/2-.08,2.55),(5.4,.16,5.1),'steel')
            corrugated('roller_door',(x-2.5,-WY/2-.17,.05),(0,0,1),(0,-1,0),4.8,5.0,'grey',depth=.03,period=.25,v=(1,0,0))
            box('door_box',(x,-WY/2-.3,5.25),(5.4,.5,.5),'steel')
            box('door_header',(x,-WY/2-.2,5.62),(5.8,.2,.22),'yellow')
            for dx in [-2.2,2.2]:
                box('dock_bumper',(x+dx,-WY/2-.25,1.0),(.35,.3,.5),'dark',.04)
                box('hazard_post',(x+dx*1.2,-WY/2-1.2,.6),(.2,.2,1.2),'yellow')
            box('door_lamp',(x,-WY/2-.4,6.1),(.6,.35,.15),'dark'); box('door_lamp_lens',(x,-WY/2-.45,6.02),(.5,.25,.03),'light')
        for x in [-10,-6,-2,2,6,10]:
            box('window',(x,-WY/2-.06,6.9),(2.6,.06,.7),'glassd'); box('window_sill',(x,-WY/2-.12,6.5),(2.8,.12,.08),'steel')
        for sx in [-1,1]:
            for sy in [-1,1]:
                cyl('downpipe',(sx*(WX/2+.12),sy*(WY/2-.4),WH/2),.08,WH,'steel',8)
        box('gutter',(0,-WY/2-.25,WH+.05),(WX+.2,.25,.2),'steel')
        box('p_door',(11,-WY/2-.06,1.9),(1,.08,2.1),'dark'); box('p_canopy',(11,-WY/2-.6,3.2),(1.6,1.1,.1),'steel')
        box('sign_board',(-5,-WY/2-.1,WH-.7),(9,.12,1.3),'white')
        for k in range(7): box('sign_letter',(-8.8+k*1.25,-WY/2-.17,WH-.7),(.8,.04,.8),'blue')
    elif kind=='crane':
        # Ship-to-shore gantry crane (D-054): four legs on rail bogies, portal and sill beams,
        # twin box-girder boom with outreach over the water and backreach, A-frame with fore and
        # back stays, machinery house, trolley with the operator cab, spreader on hoist ropes,
        # stairs, walkway rails, hazard striping. Boom along -Y (waterside).
        for sx in [-9,9]:
            for sy in [-8,8]:
                box('leg',(sx,sy,17),(1.3,1.3,34),'rust')
                box('bogie',(sx,sy,.6),(1.8,3.4,1.2),'dark',.05)
                for dy in [-1.1,1.1]: cyl('bogie_wheel',(sx,sy+dy,.45),.42,.5,'steel',12).rotation_euler=(0,math.pi/2,0)
                for k in range(4): box('hazard',(sx,sy,2+k*.9),(1.36,1.36,.45),'yellow' if k%2==0 else 'dark')
            box('sill',(sx,0,1.8),(1.1,17.3,1.1),'rust')
            beam('leg_brace',(sx,-8,3),(sx,8,15),.5,'rust'); beam('leg_brace',(sx,8,3),(sx,-8,15),.5,'rust')
        for sy in [-8,8]:
            for z in [15,33.5]: box('portal',(0,sy,z),(19.3,1.3,1.4),'rust')
        for bx in [-3.5,3.5]:
            box('boom_girder',(bx,-12,35.2),(1.4,68,2.4),'rust')
            for yy in range(-44,22,6): box('boom_web',(bx,yy,35.2),(1.45,.12,2.45),'orange')
            box('walkway_rail',(bx*1.28,-12,36.9),(.06,66,.06),'yellow')
        for yy in range(-44,22,11): box('boom_tie',(0,yy,36.3),(8.4,.5,.4),'rust')
        for bx in [-3.5,3.5]: beam('a_frame',(bx,-1,36.4),(0,2,54),.9,'rust')
        box('apex',(0,2,54.3),(2.5,2,1.2),'rust')
        for bx in [-2.5,2.5]:
            beam('fore_stay',(bx*.3,1.5,54),(bx,-44,36.5),.28,'steel')
            beam('back_stay',(bx*.3,2.5,54),(bx,21,36.5),.28,'steel')
        box('machinery_house',(0,15,39.4),(9,10,5),'white',.08)
        for yy in [11,13.5,16,18.5]: box('mh_window',(-4.52,yy,40),(.05,1.5,.8),'glassd')
        box('mh_roof',(0,15,42),(9.4,10.4,.2),'steel')
        box('trolley',(0,-24,33.4),(8.6,5,1.6),'yellow',.05)
        box('cab',(3,-25.5,31.2),(2.4,3,2.6),'white',.06)
        box('cab_glass',(3,-27.02,31.2),(2.1,.06,2.0),'glassd'); box('cab_floor_glass',(3,-25.5,29.88),(2.1,2.6,.05),'glassd')
        for dx in [-2.8,2.8]:
            for dy in [-1.8,1.8]: beam('rope',(dx*.5,-24+dy,32.5),(dx*.6,-24+dy*1.2,14),.05,'dark')
        box('headblock',(0,-24,14),(4,2.2,.8),'yellow')
        box('spreader',(0,-24,13.1),(12.2,2.44,.5),'yellow')
        for sx in [-6,6]:
            for sy in [-1.1,1.1]: box('twistlock',(sx,-24+sy,12.75),(.25,.25,.25),'dark')
        for k in range(10):  # stair flights zig-zagging up the landside leg
            z=1.5+k*3.2; d=1 if k%2==0 else -1
            beam('stair',(10.2,8+d*1.2,z),(10.2,8-d*1.2,z+3.2),.7,'steel')
            box('landing',(10.2,8+(-d)*1.2,z+3.2),(1.2,1.2,.08),'steel')
        box('boom_tip_lamp',(0,-45.8,35.5),(1.2,.4,.6),'light')
        cyl('flag_mast',(-3.5,21.5,38),.06,3,'steel',6)
    elif kind=='ship':
        # Feeder container ship moored at the quay (D-054), bow +Y, waterline at z = -1.1.
        stations=[(-84,12.5,-4),(-70,14,-6),(-40,14,-7),(40,14,-7),(62,12.5,-6.5),(76,8,-5),(84,2.5,-3),(86,.4,0)]
        vs=[]; fs=[]; ring=[]
        for y,w,keel in stations:
            sec=[(0,keel),(w*.85,keel),(w,keel+2.5),(w,9),(w*.98,9.6)]
            full=sec+[(-x,z) for x,z in reversed(sec)]
            ring.append(len(vs)); vs.extend((x,y,z) for x,z in full)
        n=10  # points per full hull section (5 per side)
        for i in range(len(stations)-1):
            for j in range(n):
                a=ring[i]+j; b=ring[i]+(j+1)%n; c2=ring[i+1]+(j+1)%n; d=ring[i+1]+j
                fs.append((a,b,c2,d))
        fs.append(tuple(range(ring[0],ring[0]+n))[::-1]); fs.append(tuple(range(ring[-1],ring[-1]+n)))
        me=bpy.data.meshes.new('hull'); me.from_pydata(vs,[],fs); me.materials.append(M['hull'])
        hull=bpy.data.objects.new('hull',me); bpy.context.scene.collection.objects.link(hull)
        import bmesh as _bm
        bm=_bm.new(); bm.from_mesh(me); _bm.ops.recalc_face_normals(bm,faces=bm.faces); bm.to_mesh(me); bm.free()
        box('boot_top',(0,-2,-1.0),(28.3,150,.9),'hull_red')
        box('deck',(0,-2,9.5),(27,156,.3),'grey')
        for sx in [-1,1]: box('bulwark',(sx*13.9,0,10.2),(.2,150,1.2),'hull')
        box('forecastle',(0,74,11),(18,14,3),'hull')
        for sx in [-1,1]:
            box('anchor',(sx*6,83,6),(1.4,.4,1.8),'dark'); box('hawse',(sx*6,83.4,7.2),(.9,.3,.9),'dark')
            for y in [70,78,-78]: cyl('bitt',(sx*10,y,10.3),.35,1.2,'dark',10)
        palette=['c_teal','c_red','c_blue','c_orange','c_grey']
        for bay in range(9):
            y=-52+bay*13.2
            box('hatch',(0,y,10.1),(24,12.4,.9),'grey')
            for col in range(0,10,2):
                x=-11+col*2.44+2.44
                rows=3+((bay*7+col*3)%3)
                box('stack',(x,y,10.6+rows*2.59/2),(4.8,12.2,rows*2.59),palette[(bay*3+col)%5])
                for r in range(1,rows): box('seam_h',(x,y,10.6+r*2.59),(4.85,12.25,.06),'dark')
                box('seam_v',(x,y,10.6+rows*2.59/2),(.06,12.25,rows*2.59),'dark')
        box('lashing_bridge',(0,-58.5,12),(24,.8,4),'steel')
        # accommodation block aft with window rows, bridge with wings, funnel, mast, lifeboat
        box('accommodation',(0,-73,17),(20,12,15),'white',.1)
        for z in [12,15,18,21]:
            for side in [-1,1]: box('win_row',(side*10.05,-73,z),(.06,10,.9),'glassd')
            box('win_row_f',(0,-66.95,z),(18,.06,.9),'glassd')
        box('bridge',(0,-68,25.5),(28,6,3),'white',.08); box('bridge_glass',(0,-64.95,25.8),(27,.06,1.4),'glassd')
        box('bridge_roof',(0,-68,27.1),(29,6.6,.25),'grey')
        box('funnel',(0,-80,24),(5,6,9),'white',.3); box('funnel_band',(0,-80,26),(5.1,6.1,2),'orange'); box('funnel_top',(0,-80,28.6),(5.2,6.2,.6),'dark')
        cyl('mast',(0,-70,32),.25,9,'steel',8); box('radar',(0,-70,30),(4,.4,.3),'dark')
        box('lifeboat',(11.5,-78,19),(2.6,7,2.4),'orange',.5)
        for yy in [-80.5,-75.5]: beam('davit',(10.5,yy,17),(12.3,yy,20.5),.25,'steel')
        for k in range(10): box('name_letter',(-9+k*1.6,85.6,6.5),(1.1,.1,1.4),'white').rotation_euler=(0,0,0)
    elif kind=='truck':
        # Terminal tractor-trailer with a 40 ft box (D-054), front +Y.
        box('trailer_frame',(0,-2,1.15),(2.3,12.4,.3),'dark')
        for dy in [-7.1,-5.9]:
            for sx in [-1,1]:
                w=cyl('wheel',(sx*1.02,dy,.52),.52,.36,'dark',16); w.rotation_euler=(0,math.pi/2,0)
                h=cyl('hub',(sx*1.21,dy,.52),.26,.04,'steel',12); h.rotation_euler=(0,math.pi/2,0)
        box('box_40',(0,-2,2.6),(2.44,12.19,2.59),'c_orange',.03)
        for sx in [-1,1]: corrugated('box_side',(sx*1.24,-8.05,1.4),(0,1,0),(sx,0,0),12.1,2.45,'c_orange')
        box('landing_legs',(0,1.5,.6),(2.0,.2,1.0),'steel')
        box('chassis',(0,5.2,.85),(1.1,5.4,.35),'dark')
        box('cab',(0,6.3,2.3),(2.4,2.3,2.7),'red',.12)
        box('cab_roof',(0,6.2,3.75),(2.2,2,.3),'red',.1)
        box('windscreen',(0,7.46,2.8),(2.1,.05,1.1),'glassd')
        for sx in [-1,1]:
            box('side_window',(sx*1.21,6.8,2.8),(.05,1,.9),'glassd')
            box('mirror',(sx*1.45,7.3,2.8),(.12,.08,.5),'dark')
            box('headlamp',(sx*.8,7.47,1.45),(.35,.05,.2),'light')
            cyl('stack',(sx*1.05,5.0,3.2),.1,2.8,'steel',10)
            for dy in [7.0,4.3,3.0]:
                w=cyl('wheel',(sx*1.0,dy,.52),.52,.32,'dark',16); w.rotation_euler=(0,math.pi/2,0)
                h=cyl('hub',(sx*1.17,dy,.52),.26,.04,'steel',12); h.rotation_euler=(0,math.pi/2,0)
            box('fuel_tank',(sx*1.05,5.2,.9),(.5,1.2,.55),'steel',.08)
        box('grille',(0,7.47,1.95),(1.4,.06,.9),'dark')
        box('bumper',(0,7.5,.8),(2.4,.2,.35),'steel')
        box('fifth_wheel',(0,3.6,1.2),(1.3,1.3,.15),'dark')
    elif kind=='light_tower':
        box('base',(0,0,.5),(3,3,1),'concrete',.05)
        cone('mast',(0,0,16),.6,.35,30,'steel')
        box('cabinet',(.9,0,1.8),(.6,.5,1.2),'grey',.03)
        box('platform',(0,0,31),(4,4,.25),'steel')
        for k in range(8): beam('rail',(-2+(k%4)*1.33,-2 if k<4 else 2,31.1),(-2+(k%4)*1.33,-2 if k<4 else 2,32.1),.05,'steel')
        for k in range(6):
            x=-1.5+(k%3)*1.5; z=32.6+(k//3)*1.2
            box('flood',(x,-.2,z),(1.2,.6,1.0),'dark',.04); box('flood_lens',(x,-.52,z),(1.0,.05,.8),'light')
        for k in range(10): box('rung',(.55,0,2+k*2.9),(.4,.05,.05),'steel')
    elif kind=='overpass':
        # Viaduct over the race road (D-059), local X along the deck, deck underside ~7.3 m up.
        # Pillar bents at |x| 15 and 40: always outside the track walls (<= 10.2 m).
        box('deck',(0,0,8.4),(100,14,1.1),'concrete',.05)
        for y in [-4.5,0,4.5]: box('girder',(0,y,7.55),(100,0.9,0.7),'grey')
        for y in [-7.2,7.2]:
            box('parapet',(0,y,9.4),(100,.4,.9),'concrete',.04)
            box('parapet_cap',(0,y,9.9),(100,.5,.1),'steel')
        for x in [-40,-15,15,40]:
            for y in [-4,4]: box('column',(x,y,3.6),(1.5,1.5,7.2),'concrete',.06)
            box('pier_cap',(x,0,7.0),(2.2,12,0.8),'concrete',.05)
        for x in range(-45,50,10): box('lane_dash',(x,0,8.97),(4,.15,.02),'white')
        for x in [-30,0,30]:
            cyl('deck_lamp',(x,7.0,12),.08,5,'steel',8); box('deck_lamp_head',(x,6.2,14.4),(.4,1.6,.18),'dark'); box('deck_lamp_lens',(x,6.2,14.3),(.3,1.2,.04),'light')
        # overhead sign on the viaduct face towards traffic (fictional destinations)
        box('sign',(0,-7.7,10.8),(10,.2,2.2),'teal',.05)
        for k in range(6): box('sign_text',(-3.6+k*1.4,-7.82,11.0),(1.0,.04,.5),'white')
        box('sign_arrow',(3.8,-7.82,10.3),(.8,.04,.8),'white')
        # traffic parked on the deck: simple boxy cars in the harbour colours
        for x,y,c in [(-28,-2.3,'red'),(-8,2.3,'blue'),(12,-2.3,'white'),(33,2.3,'orange')]:
            box('deck_car',(x,y,9.55),(4.2,1.8,1.0),c,.2); box('deck_car_top',(x-.3,y,10.3),(2.2,1.6,.6),'glassd',.12)
    elif kind=='billboard':
        # Trackside advertising for fictional brands (no real trademarks).
        for x in [-4.5,4.5]: box('post',(x,0,3),(.35,.35,6),'steel')
        box('frame',(0,0,6.6),(12.4,.4,4.4),'steel')
        box('board',(0,-.22,6.6),(12,.05,4),'white')
        box('panel_a',(-3,-.26,6.6),(6,.04,4),'boardc')
        cyl('logo',(3.2,-.28,6.6),1.4,.04,'yellow',24).rotation_euler=(math.pi/2,0,0)
        for k in range(5): box('letter',(-5.2+k*1.05,-.3,6.0),(.8,.03,1.0),'white')
        for x in [-4,0,4]: box('bb_lamp',(x,-1.2,9.1),(.8,.4,.2),'dark')
    objs=list(bpy.context.scene.objects)
    for o in objs: o.select_set(True)
    bpy.context.view_layer.objects.active=objs[0]; bpy.ops.object.join(); o=bpy.context.object; o.name='harbor_'+kind
    bpy.context.scene.cursor.location=(0,0,0); bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
    o['asset_id']=o.name; o['units']='metres'; o['authoring_axes']='Z up; directional props face -Y (not vehicles); no runtime axis assumption'; o['collision']='separate compound-box collision GLB'
    return o

def export(path,objects):
    bpy.ops.object.select_all(action='DESELECT')
    for o in objects: o.select_set(True)
    bpy.ops.export_scene.gltf(filepath=str(path),export_format='GLB',use_selection=True,export_yup=True,export_extras=True)
def digest(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def render(path,location,target,scale,res=(1500,1000)):
    bpy.ops.object.camera_add(location=location); cam=bpy.context.object; cam.rotation_euler=(Vector(target)-cam.location).to_track_quat('-Z','Y').to_euler(); cam.data.type='ORTHO'; cam.data.ortho_scale=scale; bpy.context.scene.camera=cam
    cam.data.clip_end=10000
    sc=bpy.context.scene; sc.render.engine='BLENDER_EEVEE_NEXT'; sc.render.resolution_x=res[0]; sc.render.resolution_y=res[1]; sc.render.resolution_percentage=100
    sc.world.color=(.2,.2,.2); bpy.ops.object.light_add(type='SUN',location=(0,0,100)); sun=bpy.context.object; sun.rotation_euler=(.4,-.5,-.3); sun.data.energy=2.5
    sc.render.image_settings.file_format='PNG'; sc.render.filepath=str(path); bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(cam,do_unlink=True); bpy.data.objects.remove(sun,do_unlink=True)

def collision_proxy(kind,visual):
    proxies=[]
    def add(loc,size):
        o=box('COL_'+kind+'_'+str(len(proxies)),loc,size,'dark'); o['collision_only']=True; o['shape']='box'; proxies.append(o)
    if kind=='start_gantry':
        for x in [-8,8]: add((x,0,3.25),(.7,1.2,6.5))
        add((0,0,6.15),(16,.5,.9))
    elif kind=='crane':
        for x in [-9,9]:
            for y in [-8,8]: add((x,y,17),(1.8,3.4,34))
        add((0,-12,35.2),(8.4,68,2.4))
    elif kind=='guardrail':
        add((0,-.1,.7),(4,.2,.5))
        for x in [-1.6,1.6]: add((x,0,.6),(.15,.17,1.2))
    elif kind=='lamp': add((0,0,4),(.2,.2,8)); add((0,-.7,7.9),(.5,1.5,.3))
    elif kind=='chevron_sign':
        add((0,0,1.65),(2,.12,.85))
        for x in [-.6,.6]: add((x,0,.9),(.1,.1,1.8))
    else:
        corners=[Vector(v) for v in visual.bound_box]; lo=Vector(tuple(min(v[i] for v in corners) for i in range(3))); hi=Vector(tuple(max(v[i] for v in corners) for i in range(3)))
        add((lo+hi)/2,hi-lo)
    p=ENV/(kind+'_collision.glb'); export(p,proxies)
    for o in proxies: bpy.data.objects.remove(o,do_unlink=True)
    return p

kinds=['container','container_red','container_blue','container_orange','container_grey','bollard','pallet','crate','concrete_barrier','guardrail','cone','lamp','chevron_sign','pickup_pad','start_gantry','warehouse','crane',
       'ship','truck','light_tower','billboard','overpass']
BUDGET={'crane':20000,'warehouse':20000,'ship':30000,'truck':8000}
kit=[]; records=[]
for kind in kinds:
    bpy.ops.object.select_all(action='DESELECT')
    # Keep previously generated assets unlinked while building/joining current one.
    for o in kit:
        for collection in list(o.users_collection): collection.objects.unlink(o)
    o=build(kind); p=ENV/(kind+'.glb'); export(p,[o]); o.data.calc_loop_triangles()
    cp=collision_proxy(kind,o)
    center=sum((Vector(v) for v in o.bound_box),Vector())/8; size=max(o.dimensions); extent=max(size*1.6,1)
    render(ENV/(kind+'_preview.png'),center+Vector((extent,-extent,extent*.8)),center,extent,(700,700))
    records.append({'asset_id':o.name,'path':str(p.relative_to(ROOT)).replace('\\','/'),'sha256':digest(p),'dimensions_m':[round(x,3) for x in o.dimensions],'triangles':len(o.data.loop_triangles),'triangle_budget':BUDGET.get(kind,6000),'collision':o['collision'],'provenance':'Original procedural geometry authored by Codex; no external textures or trademarked models.'})
    records[-1].update(collision_path=str(cp.relative_to(ROOT)).replace('\\','/'),collision_sha256=digest(cp),preview_path=str((ENV/(kind+'_preview.png')).relative_to(ROOT)).replace('\\','/'))
    kit.append(o)
    for old in kit[:-1]: bpy.context.scene.collection.objects.link(old)
for i,o in enumerate(kit): o.location=((i%4)*32,(i//4)*34,0)
box('display_floor',(45,45,-.3),(160,160,.5),'concrete')
bpy.ops.wm.save_as_mainfile(filepath=str(ENV/'harbor_kit.blend'))
render(ENV/'harbor_kit_preview.png',(145,-140,155),(43,40,4),170)
# Preserve data for instancing but clear scene.
for o in list(bpy.context.scene.objects):
    for collection in list(o.users_collection): collection.objects.unlink(o)
# ---------------------------------------------------------------------------------------------
# Tracks. The centreline is the single source of truth: the game builds its seamless drive
# surface and continuous walls from content/tracks/<id>_*.json; the Blender scene is
# presentation only (plan 3.5: visual seams must never become physical bumps).
# build_track() sets these module globals for the helpers below (one track at a time).
WALL_OFFSET=2.2         # barrier face beyond the road edge; must match the game's wall builder
N=0; HALF_WIDTH=0.0; pts=[]; cum=[]; LENGTH=0.0
ELEV=[]; DROPS=set()   # per-point drive-surface height and jump lips (D-059), mirrored by the game
def H(s):
    """Drive-surface height at distance s - the same rule as racer::TrackRoute::ElevationAt."""
    if not ELEV: return 0.0
    s%=LENGTH
    i=max(k for k in range(N) if cum[k]<=s)
    t=(s-cum[i])/max(1e-6,cum[i+1]-cum[i])
    if i in DROPS: return ELEV[i] if t<=1e-4 else 0.0
    return ELEV[i]+(ELEV[(i+1)%N]-ELEV[i])*t
def frame(i):
    d=(pts[(i+1)%N]-pts[(i-1)%N]).normalized(); return d,Vector((-d.y,d.x,0))
def at(s):
    # Point, direction and left normal at distance s along the closed centreline.
    s%=LENGTH
    i=max(k for k in range(N) if cum[k]<=s)
    p0,p1=pts[i],pts[(i+1)%N]; t=(s-cum[i])/max(1e-6,cum[i+1]-cum[i])
    d=(p1-p0).normalized(); q=p0.lerp(p1,t); q=Vector((q.x,q.y,H(s))); return q,d,Vector((-d.y,d.x,0))
def dist_to_road(q):
    return min(((q-pts[i]).to_2d()).length for i in range(N))
def ribbon(name,inner,outer,z,material):
    # One quad per segment, heights from ELEV (D-059); a segment after a jump lip lies on the
    # ground (the gap), so its vertices are not shared with the raised lip row.
    inner,outer=sorted((inner,outer))
    vs=[]; fs=[]; uvs=[]
    tile=TILE.get(material,6.0)
    for i in range(N):
        ha,hb=((0.0,0.0) if i in DROPS else (ELEV[i],ELEV[(i+1)%N])) if ELEV else (0.0,0.0)
        k=len(vs)
        for j,h in ((i,ha),(i+1,hb)):
            p=pts[j%N]; n=frame(j%N)[1]
            for w in [inner,outer]: vs.append(tuple(p+n*w+Vector((0,0,z+h)))); uvs.append((w/tile,cum[j]/tile))
        fs.append((k,k+2,k+3,k+1))
    mesh=bpy.data.meshes.new(name); mesh.from_pydata(vs,[],fs); mesh.materials.append(M[material])
    uv=mesh.uv_layers.new(name='UVMap')
    for poly in mesh.polygons:
        for li in poly.loop_indices: uv.data[li].uv=uvs[mesh.loops[li].vertex_index]
    o=bpy.data.objects.new(name,mesh); bpy.context.scene.collection.objects.link(o); return o
def dashes(name,lateral,width,dash,gap,z,material):
    """Painted dashed line as one mesh (one draw call), following the centreline."""
    vs=[]; fs=[]; s=0.0
    while s+dash<LENGTH-gap:
        for t in (s,s+dash):
            p,d,n=at(t)
            for w in (lateral-width/2,lateral+width/2): vs.append(tuple(p+n*w+Vector((0,0,z))))
        k=len(vs)-4; fs.append((k,k+2,k+3,k+1)); s+=dash+gap
    mesh=bpy.data.meshes.new(name); mesh.from_pydata(vs,[],fs); mesh.materials.append(M[material])
    o=bpy.data.objects.new(name,mesh); bpy.context.scene.collection.objects.link(o); return o
def instance(kind,loc,angle=0):
    proto=kit[kinds.index(kind)]; o=bpy.data.objects.new(proto.name,proto.data); bpy.context.scene.collection.objects.link(o); o.location=loc; o.rotation_euler.z=angle; return o
PAINT_KINDS=['container','container_red','container_blue','container_orange','container_grey']
ROW_ABILITIES=[['Lance','Surge','Ward'],['Needle','Mend','Trap'],['Pulse','Lance','Surge'],['Storm','Ward','Needle'],
               ['Trap','Surge','Lance'],['Mend','Pulse','Needle'],['Surge','Storm','Ward'],['Lance','Trap','Mend'],
               ['Needle','Surge','Pulse'],['Ward','Lance','Trap'],['Surge','Needle','Mend'],['Pulse','Ward','Lance']]

def quads_mesh(name,quads,material,z):
    vs=[]; fs=[]
    for q in quads:
        k=len(vs); vs.extend((p.x,p.y,p.z+z) for p in q); fs.append((k,k+1,k+2,k+3))
    me=bpy.data.meshes.new(name); me.from_pydata(vs,[],fs); me.materials.append(M[material])
    for p in me.polygons:
        if p.normal.z<0: p.flip()
    o=bpy.data.objects.new(name,me); bpy.context.scene.collection.objects.link(o); return o

def curvature(s):
    """Signed curvature (1/m, + = left turn) at distance s, from the centreline 6 m either side."""
    _,d0,_=at(s-6); _,d1,_=at(s+6)
    return math.atan2(d0.x*d1.y-d0.y*d1.x,d0.dot(d1))/12.0

def road_dressing(spec):
    """Visual-only road detail (D-054): red-white kerbs through corners, manhole covers, repair
    patches, tyre marks at the apexes, chevron boards outside tight corners, trackside billboards.
    Flat quads just above the road: the physical drive surface is untouched (plan 3.5)."""
    rnd=random.Random(sum(ord(ch) for ch in spec['id']))  # deterministic (str hash is salted per run)
    red=[]; white=[]; s=0.0; k=0
    while s<LENGTH:
        if abs(curvature(s+.75))>1/140:
            for side in [-1,1]:
                q=[]
                for t,w in ((s,HALF_WIDTH-.25),(s+1.5,HALF_WIDTH-.25),(s+1.5,HALF_WIDTH+.55),(s,HALF_WIDTH+.55)):
                    p,d,n=at(t); q.append(p+n*side*w)
                (red if k%2==0 else white).append(q)
        s+=1.5; k+=1
    # Layer heights a few cm apart (road .012): 2 mm gaps flickered at distance.
    if red: quads_mesh('kerb_red',red,'kerb',.06)
    if white: quads_mesh('kerb_white',white,'kerb_white',.06)
    patches=[]
    for _ in range(int(LENGTH/60)):
        s0=rnd.uniform(0,LENGTH); ln=rnd.uniform(2,6); lat=rnd.uniform(-HALF_WIDTH+1.5,HALF_WIDTH-2.5); wd=rnd.uniform(1.2,2.5)
        q=[]
        for t,w in ((s0,lat),(s0+ln,lat),(s0+ln,lat+wd),(s0,lat+wd)):
            p,d,n=at(t); q.append(p+n*w)
        patches.append(q)
    quads_mesh('road_patch',patches,'patch',.03)
    for i,s0 in enumerate(range(40,int(LENGTH),97)):
        p,d,n=at(s0); c=p+n*(3.2 if i%2 else -3.2)
        cyl('manhole',(c.x,c.y,.035),.35,.012,'manhole',16)
    # tyre marks: pairs of dark strips through each tight corner, biased to the inside
    marks=[]; s=0.0
    while s<LENGTH:
        cv=curvature(s)
        if abs(cv)>1/110:
            inside=1 if cv>0 else -1
            for lane in [rnd.uniform(.5,HALF_WIDTH-2.5)]:
                for off in [0,1.6]:
                    for t in range(0,24,2):
                        q=[]
                        for tt,w in ((s+t,lane+off),(s+t+2,lane+off),(s+t+2,lane+off+.22),(s+t,lane+off+.22)):
                            p,d,n=at(tt); q.append(p+n*inside*w)
                        marks.append(q)
            s+=40
        s+=8
    if marks: quads_mesh('tyre_marks',marks,'skid',.045)
    # chevron boards outside tight corners, facing the road
    s=0.0
    while s<LENGTH:
        cv=curvature(s)
        if abs(cv)>1/95:
            o=-1 if cv>0 else 1
            p,d,n=at(s); q=p+n*o*(HALF_WIDTH+WALL_OFFSET+1.6)
            instance('chevron_sign',q,math.atan2(-o*n.x,o*n.y))
            s+=18
        else:
            s+=6
    # billboards on the straights, opposite the lamp posts, away from any skip range
    s=100.0
    while s<LENGTH-100:
        if abs(curvature(s))<1/400 and not any(a*LENGTH<=s<=b*LENGTH for a,b in spec.get('billboard_skip',[])):
            p,d,n=at(s); q=p-n*(HALF_WIDTH+WALL_OFFSET+6)
            instance('billboard',q,math.atan2(n.x,-n.y))
            s+=260
        else:
            s+=20

def pickup_rows():
    return [90+k*180 for k in range(int((LENGTH-150)//180))]

def find_straight(near,length,span=0.14):
    """Straightest window of `length` metres around fraction `near` of the lap, clear of the start
    grid, the finish and every pickup row (+-30 m)."""
    best=None
    for k in range(int(LENGTH*(near-span)),int(LENGTH*(near+span)),5):
        s0=k%LENGTH
        if s0<80 or s0+length>LENGTH-150: continue
        if any(s0-30<=r<=s0+length+30 for r in pickup_rows()): continue
        worst=max(abs(curvature(s0+d)) for d in range(0,int(length)+1,6))
        if best is None or worst<best[0]: best=(worst,s0)
    return best[1] if best else None

def apply_features(spec):
    """Heights per centreline point from the track's features (D-059): 'jump' (ramp, flat deck,
    lip), 'hump' (a bridge-like crest that lifts the car), 'overpass' (a viaduct over the road,
    visual only). Returns the placed features for the checks file."""
    global ELEV,DROPS
    ELEV=[0.0]*N; DROPS=set(); placed=[]
    for f in spec.get('features',[]):
        if f['type']=='jump':
            s0=find_straight(f['near'],f['ramp']+f['deck']+40)
            if s0 is None: continue
            lip=None
            for i in range(N):
                d=cum[i]-s0
                if 0<=d<=f['ramp']: ELEV[i]=max(ELEV[i],f['h']*(d/f['ramp'])**0.8)
                elif f['ramp']<d<=f['ramp']+f['deck']: ELEV[i]=f['h']; lip=i
            if lip is not None: DROPS.add(lip)
            placed.append({'type':'jump','s':round(s0,1),'height_m':f['h'],'lip_index':lip})
        elif f['type']=='hump':
            s0=find_straight(f['near'],f['len']+20)
            if s0 is None: continue
            for i in range(N):
                d=cum[i]-s0
                if 0<=d<=f['len']: ELEV[i]=max(ELEV[i],f['h']*math.sin(math.pi*d/f['len']))
            placed.append({'type':'hump','s':round(s0,1),'height_m':f['h']})
        elif f['type']=='overpass':
            s0=find_straight(f['near'],30)
            if s0 is None: continue
            p,d,n=at(s0+15); instance('overpass',Vector((p.x,p.y,0)),math.atan2(n.y,n.x))
            placed.append({'type':'overpass','s':round(s0+15,1)})
    return placed

def deck_visuals():
    """Concrete sides under every raised segment, a hazard-striped face at each jump lip."""
    side=[]; lipq=[]; stripes=[]
    W=HALF_WIDTH+WALL_OFFSET+0.6
    for i in range(N):
        a,b=ELEV[i],ELEV[(i+1)%N]
        if i in DROPS or (a<=0 and b<=0): continue
        pa,na=pts[i],frame(i)[1]; pb,nb=pts[(i+1)%N],frame((i+1)%N)[1]
        for sgn in (-1,1):
            qa=pa+na*sgn*W; qb=pb+nb*sgn*W
            side.append([Vector((qa.x,qa.y,-0.1)),Vector((qb.x,qb.y,-0.1)),Vector((qb.x,qb.y,b)),Vector((qa.x,qa.y,a))])
    for i in DROPS:
        p,n=pts[i],frame(i)[1]; h=ELEV[i]
        l=p-n*W; r=p+n*W
        lipq.append([Vector((l.x,l.y,-0.1)),Vector((r.x,r.y,-0.1)),Vector((r.x,r.y,h)),Vector((l.x,l.y,h))])
        k=0; w=-W
        while w<W:  # yellow/black hazard band along the top of the lip face
            q0=p+n*w; q1=p+n*min(W,w+1.2)
            (stripes if k%2==0 else []).append([Vector((q0.x,q0.y,h-0.55)),Vector((q1.x,q1.y,h-0.55)),Vector((q1.x,q1.y,h-0.05)),Vector((q0.x,q0.y,h-0.05))])
            w+=1.2; k+=1
    def mesh(name,quads,material,push=0.0):
        vs=[]; fs=[]
        for q in quads:
            k=len(vs); vs.extend(tuple(v) for v in q); fs.append((k,k+1,k+2,k+3))
        if not quads: return
        me=bpy.data.meshes.new(name); me.from_pydata(vs,[],fs); me.materials.append(M[material])
        o=bpy.data.objects.new(name,me); bpy.context.scene.collection.objects.link(o)
        import bmesh as _bm
        bm=_bm.new(); bm.from_mesh(me)
        for f in bm.faces:  # both sides visible: duplicate reversed
            pass
        bm.free()
        me.update()
    # double-sided by adding reversed copies (the renderer culls back faces)
    both=lambda qs: qs+[list(reversed(q)) for q in qs]
    mesh('deck_side',both(side),'concrete'); mesh('jump_lip',both(lipq),'concrete'); mesh('lip_hazard',both(stripes),'yellow')

def build_track(spec):
    """Road, barriers, lamps, scenery, pickups, grid; exports the visual GLB, the game JSON,
    minimap and checks. spec: id, name, stem, folder, points (closed centreline, metres),
    half_width, quay (cx, cy, sx, sy), scenery(ctx) callback, title."""
    global N,HALF_WIDTH,pts,cum,LENGTH
    for o in list(bpy.context.scene.objects):
        if o.name not in [k.name for k in kit]: bpy.data.objects.remove(o,do_unlink=True)
    for o in list(bpy.context.scene.objects):
        for collection in list(o.users_collection): collection.objects.unlink(o)
    pts=spec['points']; N=len(pts); HALF_WIDTH=spec['half_width']
    cum=[0.0]
    for i in range(N): cum.append(cum[-1]+(pts[(i+1)%N]-pts[i]).length)
    LENGTH=cum[-1]
    folder=spec['folder']; folder.mkdir(parents=True,exist_ok=True)
    features=apply_features(spec)
    # Quay top at z = 0: the drive surface the game collides with is flat at 0 everywhere.
    cx,cy,sx,sy=spec['quay']
    box('quay_land',(cx,cy,-.5),(sx,sy,1),'quay'); box('water',(cx,cy,-1.2),(sx+450,sy+420,.1),'water')
    road=ribbon('race_road',-HALF_WIDTH,HALF_WIDTH,.012,'asphalt')
    for side in [-1,1]:
        ribbon('edge_line',side*(HALF_WIDTH-.4),side*(HALF_WIDTH-.2),.05,'white')
        ribbon('shoulder',side*HALF_WIDTH,side*(HALF_WIDTH+WALL_OFFSET-.2),.008,'dark')
        if 'apron' in M:  # D-086: a worn asphalt verge beyond the barrier, then the quay
            ribbon('apron',side*(HALF_WIDTH+WALL_OFFSET+.3),side*(HALF_WIDTH+WALL_OFFSET+6.5),.006,'apron')
    dashes('centre_line',0.0,.15,3.0,9.0,.05,'white')
    # Continuous barrier line: 3 m kit pieces every 3 m, the barrier face at the wall offset.
    barrier_count=0
    s=0.0
    while s<LENGTH:
        p,d,n=at(s+1.5); a=math.atan2(d.y,d.x)
        for side in [-1,1]:
            # kit barrier: long axis X, 0.38 m thick wall centred on its origin
            instance('concrete_barrier',p+n*side*(HALF_WIDTH+WALL_OFFSET+.19),a); barrier_count+=1
        s+=3.0
    lamps=[]
    for k in range(0,int(LENGTH),60):
        p,d,n=at(k); a=math.atan2(d.y,d.x)
        q=p+n*(HALF_WIDTH+WALL_OFFSET+3); r=a  # arm (kit -Y) overhangs the road
        instance('lamp',q,r)
        # lens centre (kit local (0,-1.15,7.86)) for the game's night street lights (D-052)
        lamps.append((q.x+1.15*math.sin(r),q.y-1.15*math.cos(r),7.8,-n.x,-n.y))
    deck_visuals()
    road_dressing(spec)
    # Scenery keeps clear of the road and barriers (validated, not assumed).
    ctx={'placed':0,'rejected':0,'yard':0,'clear':HALF_WIDTH+WALL_OFFSET+8}
    def place(kind,loc,angle,radius):
        if dist_to_road(Vector(loc))-radius<ctx['clear']: ctx['rejected']+=1; return None
        ctx['placed']+=1; return instance(kind,loc,angle)
    ctx['place']=place
    spec['scenery'](ctx)
    # Start gantry legs stand outside the walls (the kit spans +-8 m; S01's wall face is at 9.2 m and
    # cars drove through the legs - owner report 2026-09-29).
    start,sd,sn=at(0.0); g=instance('start_gantry',start,math.atan2(sd.y,sd.x)-math.pi/2); g.scale.x=(HALF_WIDTH+WALL_OFFSET+1.4)/8.0
    # Pickup rows: 3 pads across the road every ~180 m, so each row offers a line choice (plan 2.11).
    pickups=[]
    for r,s_row in enumerate(pickup_rows()):
        p,d,n=at(s_row)
        for c,lat in enumerate([-4.0,0.0,4.0]):
            q=p+n*lat; instance('pickup_pad',q)
            pickups.append({'x':q.x,'z':q.y,'ability':ROW_ABILITIES[r%len(ROW_ABILITIES)][c]})
    # Grid: 20 slots in two columns, 8 m rows, staggered by 4 m, behind the line.
    grid=[]
    for k in range(20):
        center,gd,gn=at(-(10+(k//2)*8+(k%2)*4)); lat=(-2.5 if k%2==0 else 2.5)
        p=center+gn*lat
        grid.append({'x':p.x,'z':p.y,'yaw':math.atan2(gd.x,gd.y)})
        o=box('grid_mark',p+Vector((0,0,.06)),(2.2,.16,.015),'white'); o.rotation_euler.z=math.atan2(gd.y,gd.x)-math.pi/2
    trackobjs=list(bpy.context.scene.objects); export(folder/(spec['stem']+'_greybox.glb'),trackobjs)
    bpy.ops.wm.save_as_mainfile(filepath=str(folder/(spec['stem']+'.blend')))
    render(folder/(spec['stem']+'_overhead.png'),(cx+750,cy-1000,1400),(cx,cy,0),max(sx,sy)*1.25,(1600,1100))
    xs=[p.x for p in pts]; ys=[-p.y for p in pts]; pad=120
    vb=f'{min(xs)-pad:.0f} {min(ys)-pad:.0f} {max(xs)-min(xs)+2*pad:.0f} {max(ys)-min(ys)+2*pad:.0f}'
    path=' '.join(('M' if i==0 else 'L')+f'{x:.2f},{y:.2f}' for i,(x,y) in enumerate(zip(xs,ys)))+' Z'
    (folder/'minimap.svg').write_text(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{vb}"><rect x="{min(xs)-pad:.0f}" y="{min(ys)-pad:.0f}" width="100%" height="100%" fill="#0b2029"/><path d="{path}" fill="none" stroke="#e1eef0" stroke-width="14"/><circle cx="{pts[0].x}" cy="{-pts[0].y}" r="15" fill="#ff982e"/><text x="{min(xs)-pad+40:.0f}" y="{min(ys)-pad+60:.0f}" fill="#63dbea" font-size="32">{spec["title"]} / {LENGTH/1000:.2f} KM</text></svg>',encoding='utf-8')
    # Game definition in runtime ground coordinates: Blender (x, y) -> Wicked (x, z) per D-002.
    game={'schema_version':1,'id':spec['id'],'name':spec['name'],'region':'R01',
          'visual_asset':str((folder/(spec['stem']+'_greybox.glb')).relative_to(ROOT)).replace('\\','/'),
          'half_width_m':HALF_WIDTH,'wall_offset_m':WALL_OFFSET,
          'centerline':[[round(p.x,4),round(p.y,4)] for p in pts],
          'checkpoints_s':[round(f*LENGTH,3) for f in (0,.25,.5,.75)],
          'grid':[[round(g['x'],4),round(g['z'],4),round(g['yaw'],5)] for g in grid],
          'pickups':[[round(p['x'],4),round(p['z'],4),p['ability']] for p in pickups]}
    if any(h>0 for h in ELEV):
        game['elevation']=[round(h,3) for h in ELEV]
        game['drops']=sorted(DROPS)
    (ROOT/'content/tracks').mkdir(parents=True,exist_ok=True)
    (ROOT/'content/tracks'/(spec['id']+'_'+spec['stem']+'.json')).write_text(json.dumps(game,indent=1),encoding='utf-8')
    # Street-light positions in runtime coordinates (x, height, z); outside content/tracks so the
    # game's <id>_*.json track lookup never picks it up.
    (ROOT/'content/lamps').mkdir(parents=True,exist_ok=True)
    (ROOT/'content/lamps'/(spec['id']+'.json')).write_text(json.dumps({'schema_version':1,'track':spec['id'],
        'lamps':[[round(x,3),round(z,3),round(y,3),round(ax,4),round(ay,4)] for x,y,z,ax,ay in lamps]},indent=1),encoding='utf-8')
    checks={'features':features,'length_m':LENGTH,'sample_count':N,'grid_count':len(grid),'pickup_count':len(pickups),'barrier_pieces':barrier_count,
            'scenery_placed':ctx['placed'],'yard_containers':ctx['yard'],'scenery_rejected_too_close':ctx['rejected'],
            'all_coordinates_finite':all(math.isfinite(v) for p in pts for v in p),
            'limits':'Geometry only here; drivability, AI completion and performance are tested in the game (racer_handling_tests).'}
    checks['road_faces_upward']=all(p.normal.z>.99 for p in road.data.polygons)
    checks['all_prop_triangle_budgets_pass']=all(r['triangles']<=r['triangle_budget'] for r in records)
    (folder/'geometric_checks.json').write_text(json.dumps(checks,indent=2),encoding='utf-8')
    files=[{'path':str(p.relative_to(ROOT)).replace('\\','/'),'sha256':digest(p),'bytes':p.stat().st_size} for p in sorted(folder.iterdir()) if p.is_file() and p.suffix in ['.glb','.blend','.png','.svg','.json'] and p.name!='manifest_fragment.json']
    (folder/'manifest_fragment.json').write_text(json.dumps({'schema_version':1,'generator':'tools/generate_environment.py','seed':7429,'provenance':'Original procedural assets generated locally with Blender. No downloaded model, texture, sound or Blur content.','assets':[],'files':files},indent=2),encoding='utf-8')
    print('TRACK_COMPLETE',spec['id'],json.dumps(checks),flush=True)
    return checks

def yard(ctx,x0,x1,y0,y1,density,avoid=()):
    """Container stacks of 1-3 boxes in mixed paints on a 9 x 16 m grid (same clearance rule)."""
    for gx in range(int(x0),int(x1)+1,9):
        for gy in range(int(y0),int(y1)+1,16):
            if random.random()>density: continue
            if any(abs(gx-wx)<22 and abs(gy-wy)<18 for wx,wy in avoid): continue
            if ctx['place'](random.choice(PAINT_KINDS),(gx,gy,0),math.pi/2,4) is None: continue
            ctx['yard']+=1
            for level in range(1,random.choice([1,1,2,2,3])):
                instance(random.choice(PAINT_KINDS),(gx,gy,2.59*level),math.pi/2+random.choice([0,math.pi])); ctx['yard']+=1

# --- S01 Dock Loop (2.2 km): wide sweeping ellipse around the warehouse block -----------------
def s01_scenery(ctx):
    place=ctx['place']
    warehouses=[(-220+(i%4)*120,-110+(i//4)*110) for i in range(12)]
    for i in range(12):
        x,y=warehouses[i]
        place('warehouse',(x,y,0),0,15)
        for j in range(5): place('container',(x+18+j*4,y-27,0),math.pi/2,4)
    for x in [-560,560]:
        # STS cranes on both quay edges, booms out over the water (boom is kit -Y)
        for y in [-180,0,180]: place('crane',(x,y,0),math.pi/2 if x>0 else -math.pi/2,20)
    # Moored feeder ships under the cranes (in the water, outside the quay slab)
    instance('ship',(592,-20,0),0); instance('ship',(-592,40,0),math.pi)
    for x,y in [(-470,-300),(-470,300),(470,-300),(470,300),(-150,-330),(150,330),(0,-330)]:
        place('light_tower',(x,y,0),0,3)
    for x,y,a in [(-380,-250,0),(-300,-250,0),(380,250,math.pi),(300,250,math.pi),(-120,270,math.pi/2),(140,-270,-math.pi/2)]:
        place('truck',(x,y,0),a,8)
    # Container yard: fills the quay so it reads as a working port, not an empty slab.
    yard(ctx,-500,520,-350,350,.12,warehouses)  # ~500 boxes (~0.4 M triangles): plan 2.5 M visible-triangle warning
S01N=256
s01=[Vector(((430+25*math.cos(3*t))*math.cos(t),(245+22*math.sin(2*t))*math.sin(t),0)) for t in [2*math.pi*i/S01N for i in range(S01N)]]
s01len=sum((s01[(i+1)%S01N]-s01[i]).length for i in range(S01N)); s01=[p*(2200/s01len) for p in s01]
build_track({'id':'S01','name':'Dock Loop','stem':'dock_loop','folder':TRACK,'points':s01,'half_width':7.0,   # 14 m road (plan 2.11: 12-18 m)
             'quay':(0,0,1150,780),'scenery':s01_scenery,'title':'DOCK LOOP',
             # D-059: a jump on the back straight, a viaduct over the front straight
             'features':[{'type':'jump','near':0.22,'ramp':34,'deck':14,'h':1.9},{'type':'overpass','near':0.7}]})

# --- L01 Container Run (3.6 km): quay straight, fast sweeper, container-corridor S-bends, north
# hairpin, long north straight with a kink. Its distinct sector (plan 2.11) is the corridor, where
# two-high container walls stand right behind the barriers.
def catmull_closed(ctrl,samples_per_seg=24):
    out=[]; n=len(ctrl)
    for i in range(n):
        p0,p1,p2,p3=ctrl[(i-1)%n],ctrl[i],ctrl[(i+1)%n],ctrl[(i+2)%n]
        for k in range(samples_per_seg):
            t=k/samples_per_seg; t2=t*t; t3=t2*t
            out.append(0.5*((2*p1)+(-p0+p2)*t+(2*p0-5*p1+4*p2-p3)*t2+(-p0+3*p1-3*p2+p3)*t3))
    return out
def resample(points,count):
    n=len(points); c=[0.0]
    for i in range(n): c.append(c[-1]+(points[(i+1)%n]-points[i]).length)
    out=[]; j=0
    for k in range(count):
        s=c[-1]*k/count
        while c[j+1]<s: j+=1
        t=(s-c[j])/max(1e-6,c[j+1]-c[j]); out.append(points[j].lerp(points[(j+1)%n],t))
    return out
L01_CTRL=[(0,-300),(400,-320),(650,-280),(760,-150),(700,0),(560,40),(460,-20),(340,40),(220,-10),(120,60),
          (40,160),(120,280),(-20,330),(-260,300),(-520,330),(-700,250),(-740,80),(-650,-60),(-760,-200),(-620,-310),(-350,-320)]
l01=resample(catmull_closed([Vector((x,y,0)) for x,y in L01_CTRL]),400)
l01len=sum((l01[(i+1)%400]-l01[i]).length for i in range(400)); l01=[p*(3600/l01len) for p in l01]
L01_TRACK=ROOT/'assets/tracks/container_run'
def l01_scenery(ctx):
    place=ctx['place']
    # Container corridor: two-high walls right behind both barriers through the S-bends
    # (visual only - the physical wall is the barrier line). They sit inside the clearance ring
    # on purpose, so they are placed directly, not through place().
    s0,s1=.30*LENGTH,.46*LENGTH
    s=s0
    while s<s1:
        p,d,n=at(s); a=math.atan2(d.y,d.x)
        for side in [-1,1]:
            q=p+n*side*(HALF_WIDTH+WALL_OFFSET+.38+1.25)
            for level in range(2):
                instance(random.choice(PAINT_KINDS),(q.x,q.y,2.59*level),a+math.pi/2); ctx['yard']+=1  # long axis along the road
        s+=6.3
    xs=[p.x for p in pts]; ys=[p.y for p in pts]
    warehouses=[(-420+i*120,-40) for i in range(4)]+[(-420+i*120,120) for i in range(4)]
    for x,y in warehouses: place('warehouse',(x,y,0),0,15)
    for x in range(int(min(xs))-60,int(max(xs))+61,180): place('crane',(x,min(ys)-95,0),0,20)
    # ships along the south quay edge (quay slab ends 130 m south of the course)
    for x in [-300,150,560]:
        instance('ship',(x,min(ys)-147,0),math.pi/2)
    for x in range(int(min(xs))+40,int(max(xs)),220):
        place('light_tower',(x,min(ys)-60,0),0,3)
        place('light_tower',(x+110,max(ys)+45,0),0,3)
    for i,x in enumerate(range(int(min(xs))+80,int(max(xs)),300)):
        place('truck',(x,min(ys)-40,0),math.pi/2 if i%2 else -math.pi/2,8)
    yard(ctx,min(xs),max(xs),min(ys),max(ys),.10,warehouses)
lx=[p.x for p in l01]; ly=[p.y for p in l01]
build_track({'id':'L01','name':'Container Run','stem':'container_run','folder':L01_TRACK,'points':l01,'half_width':8.0,   # 16 m: 20-car fields (E07)
             'quay':((min(lx)+max(lx))/2,(min(ly)+max(ly))/2,max(lx)-min(lx)+300,max(ly)-min(ly)+260),'scenery':l01_scenery,'title':'CONTAINER RUN',
             'billboard_skip':[(.28,.48)],  # the container corridor stands right behind the barriers there
             # D-059: a hump bridge that lifts the cars, a jump after the corridor, a viaduct
             'features':[{'type':'hump','near':0.2,'len':80,'h':2.2},{'type':'jump','near':0.56,'ramp':30,'deck':12,'h':1.7},
                         {'type':'overpass','near':0.84}]})

files=[{'path':str(p.relative_to(ROOT)).replace('\\','/'),'sha256':digest(p),'bytes':p.stat().st_size} for p in sorted(ENV.iterdir()) if p.is_file() and p.suffix in ['.glb','.blend','.png','.svg','.json'] and p.name!='manifest_fragment.json']
(ENV/'manifest_fragment.json').write_text(json.dumps({'schema_version':1,'generator':'tools/generate_environment.py','seed':7429,'provenance':'Original procedural assets generated locally with Blender. No downloaded model, texture, sound or Blur content.','assets':records,'files':files},indent=2),encoding='utf-8')
print('HARBOR_GENERATION_COMPLETE',flush=True)
