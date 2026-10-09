"""Original procedural racing cars (lofted class-A body shell, D-034). Run with Blender --background --python.
Metres; Z up; nose +Y (D-002). Geometry is authored nose -Y and turned once in face_plus_y. No third-party mesh/image inputs.
"""
import bpy, bmesh, math, json, hashlib, os
from pathlib import Path
from mathutils import Vector, Matrix

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'assets'/'vehicles'; OUT.mkdir(parents=True,exist_ok=True)
SPECS=[('D01','Tern', 'hatch',4.05,1.78,1.46,2.53,(0.015,.58,.72,1)),
       ('D02','Kestrel','coupe',4.38,1.83,1.32,2.62,(.95,.39,.025,1)),
       ('D03','Sable','sedan',4.72,1.91,1.46,2.86,(.13,.17,.23,1)),
       ('D04','Bison','pickup',4.86,1.98,1.66,2.96,(.63,.035,.028,1))]
LOD=0; PARTS=[]; M={}
def mat(n,c,metal=0,rough=.4,emit=0):
 m=bpy.data.materials.new(n); m.diffuse_color=c; m.use_nodes=True
 p=m.node_tree.nodes.get('Principled BSDF'); p.inputs['Base Color'].default_value=c
 p.inputs['Metallic'].default_value=metal;p.inputs['Roughness'].default_value=rough
 if emit: p.inputs['Emission Color'].default_value=c;p.inputs['Emission Strength'].default_value=emit
 return m
def paint_mat(n,c):
 # Automotive paint (plan 3.6): coloured dielectric base with a light metallic flake, under a
 # glossy clear coat. Exported as KHR_materials_clearcoat. The earlier metallic=0.7 paint had
 # nothing to reflect and read as near-black in game.
 m=mat(n,c,.15,.38); p=m.node_tree.nodes.get('Principled BSDF')
 p.inputs['Coat Weight'].default_value=1.0; p.inputs['Coat Roughness'].default_value=.05
 return m
def add(o,n,ma):
 o.name=n
 if ma:o.data.materials.append(M[ma])
 PARTS.append(o);return o
def bevel(o,r=.035):
 if LOD<3:
  m=o.modifiers.new('Manufactured edge radii','BEVEL');m.width=r;m.segments=3 if LOD==0 else 2
  m=o.modifiers.new('Weighted corner normals','WEIGHTED_NORMAL')
 return o
def box(n,loc,scale,ma,bev=.025):
 bpy.ops.mesh.primitive_cube_add(size=1,location=loc);o=bpy.context.object;o.scale=scale
 bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
 add(o,n,ma)
 if bev:bevel(o,bev)
 return o
def mesh(n,v,f,ma):
 me=bpy.data.meshes.new(n);me.from_pydata(v,[],f);me.update();o=bpy.data.objects.new(n,me)
 bpy.context.collection.objects.link(o);add(o,n,ma);return o
def panel(n,v,ma):return mesh(n,v,[tuple(range(len(v)))],ma)
def cyl(n,loc,r,depth,ma,rotation=(0,math.pi/2,0),vertices=None):
 bpy.ops.mesh.primitive_cylinder_add(vertices=vertices or [48,32,20,12][LOD],radius=r,depth=depth,location=loc,rotation=rotation)
 o=add(bpy.context.object,n,ma)
 for p in o.data.polygons:p.use_smooth=True
 return o
def torus(n,loc,major,minor,ma):
 bpy.ops.mesh.primitive_torus_add(major_segments=[64,40,24,16][LOD],minor_segments=[16,12,8,6][LOD],location=loc,rotation=(0,math.pi/2,0),major_radius=major,minor_radius=minor)
 o=add(bpy.context.object,n,ma)
 for p in o.data.polygons:p.use_smooth=True
 return o
def empty(n,loc):
 o=bpy.data.objects.new(n,None);bpy.context.collection.objects.link(o);o.location=loc;PARTS.append(o);return o
def body(n,sections,ma):
 v=[]
 for y,w,b,t in sections:
  v.extend([(-w*.90,y,b),(w*.90,y,b),(w,y,t-.16),(w*.9,y,t),(-w*.9,y,t),(-w,y,t-.16)])
 f=[tuple(reversed(range(6)))]
 for i in range(len(sections)-1):
  for j in range(6):f.append((i*6+j,i*6+(j+1)%6,(i+1)*6+(j+1)%6,(i+1)*6+j))
 f.append(tuple(range((len(sections)-1)*6,len(sections)*6)))
 return bevel(mesh(n,v,f,ma),.035)
def cut_arch(o,y,r,w):
 bpy.ops.mesh.primitive_cylinder_add(vertices=[64,48,28,16][LOD],radius=r,depth=w*1.4,location=(0,y,.36),rotation=(0,math.pi/2,0))
 cutter=bpy.context.object
 mod=o.modifiers.new('Actual wheel clearance','BOOLEAN');mod.operation='DIFFERENCE';mod.object=cutter
 bpy.context.view_layer.objects.active=o;bpy.ops.object.modifier_apply(modifier=mod.name)
 bpy.data.objects.remove(cutter,do_unlink=True)
def ellipsoid(n,loc,radii,ma):
 bpy.ops.mesh.primitive_uv_sphere_add(segments=[24,16,10,8][LOD],ring_count=[12,8,6,4][LOD],radius=1,location=loc)
 o=bpy.context.object;o.scale=radii;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
 for p in o.data.polygons:p.use_smooth=True
 return add(o,n,ma)
def body_half_width(L,W,y):
 # Plan view: superellipse, full width through the middle, rounded nose and tail.
 t=min(abs(y)/(L/2),.999);return W/2*(1-t**4.5)**(1/4.5)
def nose_y(L,W,x,tail=False):
 # Where the plan-view outline reaches half-width |x| (for placing lamps on the curved nose/tail).
 t=(1-min(abs(x)/(W/2),.999)**4.5)**(1/4.5);return (L/2*t) if tail else (-L/2*t)
def cos_interp(keys,y):
 if y<=keys[0][0]:return keys[0][1]
 for (y0,v0),(y1,v1) in zip(keys,keys[1:]):
  if y<=y1:
   s=(1-math.cos(math.pi*(y-y0)/max(y1-y0,1e-6)))/2;return v0+(v1-v0)*s
 return keys[-1][1]
PROF={}
def ring(y):
 """Right half of the body cross-section at station y, 11 points bottom-centre -> top-centre.
 One template everywhere: the greenhouse (g>0) grows out of the hood/deck crown, so the
 windshield and rear glass are real slopes of the same surface, not separate panels."""
 P=PROF;L=P['L'];W=P['W'];H=P['H']
 w=body_half_width(L,W,y);t=abs(y)/(L/2)
 zb=.27+.09*max(0,(t-.72)/.28)**2
 zbelt=cos_interp(P['belt'],y)
 g=cos_interp(P['cabin'],y)
 gh=g*(H-.02-zbelt);crown=.045*(1-g)+.02
 hs=zbelt-zb;wr=w*(.90-.20*g)
 return [(0,zb),(w*.80,zb),(w*.95,zb+.05),(w,zb+.40*hs),(w*.985,zb+.78*hs),(w*.93,zbelt),
         (wr*1.01,zbelt+.012),(wr,zbelt+.012+.82*gh+.3*crown),(wr*.90,zbelt+.02+.96*gh+.6*crown),
         (wr*.52,zbelt+.025+gh+.9*crown),(0,zbelt+.025+gh+crown)]
def body_shell(id,kind,L,W,H,WB,top,cf,rf,rb,cb,wh):
 PROF.update(L=L,W=W,H=H,
  belt=[(-L/2,top*.80),(-WB/2,top*.90),(cf,top*.99),(cb,top*1.0),(L/2,top*(.95 if kind=='coupe' else .98))],
  cabin=[(cf,0),(rf,1),(rb,1),(cb,0)])
 n=[34,24,16,10][LOD]
 # Stations denser at the rounded ends; stop short of the tips and cap with a fan.
 ys=[-L/2*.985*math.cos(math.pi*i/(n-1)) for i in range(n)]
 for key in (cf,rf,rb,cb):  # exact stations on the glass edges keep material borders straight
  j=min(range(n),key=lambda k:abs(ys[k]-key));ys[j]=key
 ys=sorted(set(ys))
 verts=[];rings=[]
 for y in ys:
  half=ring(y);loop=half+[(-x,z) for x,z in reversed(half[1:-1])]
  rings.append(len(verts));verts.extend((x,y,z) for x,z in loop)
 R=len(half)*2-2;faces=[];mats=[]
 ymid=(rf+rb)/2
 for i in range(len(ys)-1):
  y0,y1=ys[i],ys[i+1];yc=(y0+y1)/2
  for k in range(R):
   a=rings[i]+k;b=rings[i]+(k+1)%R;c2=rings[i+1]+(k+1)%R;d=rings[i+1]+k
   faces.append((a,d,c2,b))
   seg=k if k<10 else R-1-k  # 0..9 counted from the bottom on either side
   m=0
   if seg==0:m=2                                            # underbody
   elif seg==6 and rf-.05<yc<rb+.05:m=2 if abs(yc-ymid)<.06 else 1   # side glass, black B-pillar
   elif seg in (7,8,9) and cf<yc<rf:m=1                        # windshield
   elif seg in (7,8,9) and rb<yc<cb and kind!='pickup':m=1     # rear glass
   elif seg in (7,8) and rb<yc<cb and kind=='pickup':m=1       # pickup cab back window
   mats.append(m)
 faces.append(tuple(reversed(range(rings[0],rings[0]+R))));mats.append(0)
 faces.append(tuple(range(rings[-1],rings[-1]+R)));mats.append(0)
 me=bpy.data.meshes.new(id+'_BODY');me.from_pydata(verts,[],faces);me.update()
 o=bpy.data.objects.new(id+'_BODY',me);bpy.context.collection.objects.link(o)
 for ma in ('paint','glass','black'):me.materials.append(M[ma])
 for poly,m in zip(me.polygons,mats):poly.material_index=m;poly.use_smooth=True
 bm=bmesh.new();bm.from_mesh(me);bmesh.ops.recalc_face_normals(bm,faces=bm.faces);bm.to_mesh(me);bm.free()  # outward normals
 o.name=id+'_BODY';PARTS.append(o)
 bpy.context.view_layer.objects.active=o
 if LOD<2:
  s=o.modifiers.new('Class-A surface','SUBSURF');s.levels=s.render_levels=[2,1][LOD]
  bpy.ops.object.modifier_apply(modifier=s.name)
 # Wheel openings: the cutter's black material lines the well, so no daylight through the body.
 for y in (-WB/2,WB/2):
  bpy.ops.mesh.primitive_cylinder_add(vertices=[64,48,28,16][LOD],radius=wh+.055,depth=W*1.4,location=(0,y,.36),rotation=(0,math.pi/2,0))
  cutter=bpy.context.object;cutter.data.materials.append(M['black'])
  mod=o.modifiers.new('Wheel arch','BOOLEAN');mod.operation='DIFFERENCE';mod.object=cutter
  mod.solver='EXACT';mod.material_mode='TRANSFER'
  bpy.context.view_layer.objects.active=o;bpy.ops.object.modifier_apply(modifier=mod.name)
  bpy.data.objects.remove(cutter,do_unlink=True)
 for poly in o.data.polygons:poly.use_smooth=True
 return o
def shut_line(n,side,y,L,W,top):
 # Thin dark band standing 2 mm proud of the body, from sill to beltline, at station y.
 pts=ring(y)[2:6];vs=[];fs=[]
 for dy in (-.004,.004):
  for x,z in pts:vs.append((side*(x+.002),y+dy,z))
 k=len(pts)
 for i in range(k-1):
  f=(i,i+1,k+i+1,k+i);fs.append(f if side>0 else tuple(reversed(f)))
 return mesh(n,vs,fs,'black')
def make_car(spec,lod):
 global PARTS,M,LOD
 LOD=lod;PARTS=[];bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
 # Deleted objects leave meshes/materials/lights orphaned; without a purge each car's .blend carried every previous car.
 bpy.data.orphans_purge(do_local_ids=True,do_linked_ids=True,do_recursive=True)
 id,name,kind,L,W,H,WB,color=spec
 M={'paint':paint_mat('Factory paint',color),'black':mat('Rubber and polymer',(.013,.019,.025,1),.05,.65),
 'glass':mat('Smoked laminated glass',(.032,.08,.12,1),0,.05),'metal':mat('Brushed alloy',(.42,.48,.53,1),.9,.24),
 'lamp':mat('LED white',(.74,.9,1,1),.1,.18,3),'tail':mat('Red lens',(.62,.006,.008,1),.25,.21,2),
 'brake':mat('Brake enamel',(.8,.07,.015,1),.4,.4),'accent':mat('Warm trim',(.7,.57,.28,1),.7,.28)}
 fy=-WB/2;ry=WB/2;wh=.36 if kind!='pickup' else .40
 top=H*.62
 # Cabin stations (nose is -Y): windshield base cf, roof front rf, roof rear rb, rear-glass base cb.
 cf=-L*.19;cb=L*(.05 if kind=='pickup' else .32 if kind=='hatch' else .25)
 rf=cf+(.68 if kind=='coupe' else .55);rb=cb-(.10 if kind=='pickup' else .30);bottom=top-.02;roof=H-.045
 shell=body_shell(id,kind,L,W,H,WB,top,cf,rf,rb,cb,wh)
 for side in [-1,1]:
  wb=body_half_width(L,W,cf+.15)
  mirror_stalk=box('MIRROR_STALK_'+str(side),(side*(wb*.9+.05),cf+.16,top+.05),(.10,.05,.035),'black',.01)
  ellipsoid('MIRROR_'+str(side),(side*(wb*.9+.12),cf+.14,top+.10),(.085,.13,.065),'paint')
  box('MIRROR_GLASS_'+str(side),(side*(wb*.9+.12),cf+.268,top+.10),(.12,.006,.075),'glass',0)
  for dy in ([0] if kind in ['coupe','pickup'] else [-.25,.5]):
   x=body_half_width(L,W,dy)
   box('DOOR_HANDLE_'+str(side)+str(dy),(side*(x*.965),dy,top-.09),(.022,.15,.028),'metal',.006)
  if LOD<3:
   # Shut lines follow the actual body surface (front door, rear door / quarter panel).
   for sy in ([cf+.02,(cf+cb)/2+.02] if kind in ['sedan','hatch'] else [cf+.02,cb-.12]):
    shut_line('SHUT_LINE_'+str(side)+'_'+str(round(sy,2)),side,sy,L,W,top)
  for y,isfront in [(fy,True),(ry,False)]:
   # Built nose -Y then turned 180deg by face_plus_y, so side<0 ends up on +X = vehicle right.
   wheelname=('FR' if side<0 else 'FL') if isfront else ('RR' if side<0 else 'RL')
   pivot=empty('WHEEL_PIVOT_'+wheelname,(side*W*.47,y,.36));start=len(PARTS)
   torus('TIRE_'+wheelname,(side*W*.47,y,.36),wh-.077,.077,'black')
   cyl('RIM_BARREL_'+wheelname,(side*W*.48,y,.36),wh*.70,.12,'metal')
   cyl('RIM_INSET_'+wheelname,(side*(W*.48+.063),y,.36),wh*.59,.008,'black')
   cyl('BRAKE_ROTOR_'+wheelname,(side*(W*.48+.066),y,.36),wh*.45,.014,'metal')
   box('CALIPER_'+wheelname,(side*(W*.48+.08),y+.105,.40),(.045,.07,.13),'brake')
   for spoke in range(5 if kind!='pickup' else 6):
    angle=spoke*math.tau/(5 if kind!='pickup' else 6)
    p=box('SPOKE_'+wheelname+str(spoke),(side*(W*.48+.083),y+math.sin(angle)*wh*.30,.36+math.cos(angle)*wh*.30),(.037,.035,wh*.58),'metal',.009)
    p.rotation_euler.x=-angle
   cyl('HUB_'+wheelname,(side*(W*.48+.11),y,.36),.058,.025,'accent')
   for obj in PARTS[start:]:
    obj.parent=pivot;obj.matrix_parent_inverse=pivot.matrix_world.inverted()
 # Nose and tail sit on the curved outline (nose_y), not on a flat plane in front of it.
 for sx in [-1,1]:
  hx=W*.32;hy=nose_y(L,W,hx)+.06;hz=top*.72  # below the hood line at the nose (belt = top*.80 there)
  ellipsoid('HEADLIGHT_HOUSING'+str(sx),(sx*hx,hy,hz),(W*.155,.14,.075),'black')
  ellipsoid('HEADLIGHT_LENS'+str(sx),(sx*hx,hy-.02,hz+.005),(W*.145,.13,.065),'glass')
  ellipsoid('HEADLIGHT_LED'+str(sx),(sx*hx,hy-.08,hz),(W*.10,.05,.03),'lamp')
  ty=nose_y(L,W,W*.30,tail=True)-.05
  ellipsoid('TAIL_LENS'+str(sx),(sx*W*.30,ty,top*.86),(W*.17,.10,.05),'tail')
  box('FRONT_INTAKE'+str(sx),(sx*W*.30,nose_y(L,W,W*.38)+.02,.40),(W*.16,.06,.07),'black',.01)
 gy=nose_y(L,W,W*.26)+.02
 box('GRILLE',(0,gy,top*.62),(W*.50,.06,.17),'black',.02)
 if LOD<3:
  for i in range(-4,5):box('GRILLE_FIN'+str(i),(i*W*.05,gy-.03,top*.62),(.008,.012,.13),'metal',0)
 box('SPLITTER',(0,nose_y(L,W,W*.42)+.04,.29),(W*.80,.16,.04),'black',.01)
 box('REAR_DIFFUSER',(0,nose_y(L,W,W*.36,tail=True)-.05,.33),(W*.70,.12,.12),'black',.01)
 box('BLANK_PLATE',(0,L/2-.012,top*.62),(.40,.014,.09),'metal',.003)
 for sx in [-1,1]:
  ex=nose_y(L,W,W*.32,tail=True)-.04
  cyl('EXHAUST'+str(sx),(sx*W*.30,ex,.34),.042,.14,'metal',(math.pi/2,0,0))
  empty('SOCKET_EXHAUST_'+str(sx),(sx*W*.30,ex+.08,.34))
 if kind=='pickup':
  # Load bed: recessed liner with side rails and tailgate on top of the rear deck.
  box('BED_LINER',(0,L*.30,top+.01),(W*.70,L*.30,.05),'black')
  for sx in [-1,1]:box('BED_RAIL'+str(sx),(sx*W*.42,L*.29,top+.12),(.09,L*.37,.21),'paint')
  box('TAILGATE_TOP',(0,L*.455,top+.10),(W*.85,.09,.19),'paint')
 elif kind=='hatch':box('ROOF_SPOILER',(0,cb-.05,H-.10),(W*.66,.20,.04),'black',.015)
 elif kind=='coupe':box('REAR_LIP',(0,L*.43,top*.99+.04),(W*.72,.12,.035),'black',.01)
 for nm,loc in [('SOCKET_CAMERA',(0,2.6,1.8)),('SOCKET_WEAPON_FRONT',(0,-L/2,.7)),('SOCKET_WEAPON_REAR',(0,L/2,.7)),('SOCKET_RESET',(0,0,.6))]:empty(nm,loc)
 face_plus_y(PARTS)
 merge_for_runtime()
 return PARTS
def join(objs,name):
 # join() keeps only the active object's modifiers, so bake every modifier first.
 bpy.ops.object.select_all(action='DESELECT')
 for o in objs:o.select_set(True)
 bpy.context.view_layer.objects.active=objs[0]
 bpy.ops.object.convert(target='MESH')
 bpy.ops.object.join()
 o=bpy.context.view_layer.objects.active;o.name=name;return o
def merge_for_runtime():
 # One body mesh (<= 8 material subsets) plus one mesh per wheel under its pivot, instead of
 # ~90 separate meshes: draw calls scale with meshes x subsets (plan 2.17 budget: 2500).
 global PARTS
 meshes=[o for o in PARTS if o.type=='MESH']
 body=[o for o in meshes if o.parent is None]
 empties=[o for o in PARTS if o.type=='EMPTY']
 merged=[join(body,'BODY')]
 for p in [e for e in empties if e.name.startswith('WHEEL_PIVOT_')]:
  kids=[c for c in p.children if c.type=='MESH']
  if kids:merged.append(join(kids,'WHEEL_'+p.name[len('WHEEL_PIVOT_'):]))
 PARTS=empties+merged
def face_plus_y(parts):
 # D-002: authoring contract is nose +Y (imports facing Wicked +Z). Turn the whole car,
 # then reset wheel pivots to identity rotation so their spin axis stays +X.
 R=Matrix.Rotation(math.pi,4,'Z')
 for o in [o for o in parts if o.parent is None]:o.matrix_world=R@o.matrix_world
 bpy.context.view_layer.update()
 for p in [o for o in parts if o.type=='EMPTY' and o.children]:
  kids=[(c,c.matrix_world.copy()) for c in p.children]
  p.rotation_euler=(0,0,0);bpy.context.view_layer.update()
  for c,m in kids:
   c.matrix_parent_inverse=p.matrix_world.inverted();c.matrix_basis=Matrix.Identity(4);bpy.context.view_layer.update();c.matrix_world=m
 bpy.context.view_layer.update()
def stats(parts):
 tris=0;deps=bpy.context.evaluated_depsgraph_get()
 for o in parts:
  if o.type=='MESH':
   ev=o.evaluated_get(deps);me=ev.to_mesh();me.calc_loop_triangles();tris+=len(me.loop_triangles);ev.to_mesh_clear()
 return tris
def export(path):
 bpy.ops.object.select_all(action='DESELECT')
 for o in PARTS:o.select_set(True)
 bpy.ops.export_scene.gltf(filepath=str(path),export_format='GLB',use_selection=True,export_apply=True,export_extras=True)
def render(path):
 bpy.ops.mesh.primitive_plane_add(size=200,location=(0,0,-.045));p=bpy.context.object;p.data.materials.append(mat('Studio floor',(.035,.05,.07,1),.15,.36))
 world=bpy.context.scene.world or bpy.data.worlds.new('Studio');bpy.context.scene.world=world;world.use_nodes=True
 world.node_tree.nodes['Background'].inputs[0].default_value=(.13,.17,.22,1);world.node_tree.nodes['Background'].inputs[1].default_value=.4
 for loc,power,size in [((-4,-4,7),1500,5),((4,-1,5),1200,4),((1,5,6),1800,3)]:
  bpy.ops.object.light_add(type='AREA',location=loc);o=bpy.context.object;o.data.energy=power;o.data.shape='DISK';o.data.size=size;o.rotation_euler=(Vector((0,0,.7))-o.location).to_track_quat('-Z','Y').to_euler()
 bpy.ops.object.camera_add(location=(6.3,7.3,4.0));cam=bpy.context.object;cam.rotation_euler=(Vector((0,0,.73))-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO';cam.data.ortho_scale=6.6
 s=bpy.context.scene;s.camera=cam;s.render.engine='CYCLES';s.cycles.samples=24;s.cycles.use_denoising=True;s.render.resolution_x=960;s.render.resolution_y=720;s.render.resolution_percentage=100;s.render.image_settings.file_format='PNG';s.render.filepath=str(path)
 bpy.ops.render.render(write_still=True)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
records=[]
for spec in SPECS:
 id,name,kind,L,W,H,WB,color=spec;d=OUT/id;d.mkdir(exist_ok=True);counts=[]
 for lod in range(4):
  parts=make_car(spec,lod);bpy.context.view_layer.update();count=stats(parts);counts.append(count)
  assert count<=[60000,30000,12000,4000][lod],(id,lod,count)
  export(d/f'{id}_LOD{lod}.glb')
  if lod==0:
   bpy.ops.wm.save_as_mainfile(filepath=str(d/f'{id}.blend'))
   render(d/f'{id}_review.png')
 # Convex proxy: explicit 8-vertex bevel-free box, conservative core body only.
 bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False);PARTS=[]
 box(id+'_COLLISION',(0,0,.60),(W*.82,L*.86,.52),'black',0);export(d/f'{id}_collision.glb')
 data={'id':id,'name':name,'class':'D','body':kind,'units':'metres','source_axes':{'up':'+Z','forward':'+Y','left':'-X'},'glb_axes':{'up':'+Y','forward':'+Z'},'nominal_dimensions_m':[W,L,H],'wheelbase_m':WB,'wheel_radius_m':.4 if kind=='pickup' else .36,'lod_triangles':counts,'material_slots':8,'status':'procedural_draft_exported_not_engine_integrated','collision':{'file':id+'_collision.glb','type':'convex_box','purpose':'conservative draft chassis','tire_collision':'runtime vehicle system required'},'paint_presets':[{'name':n,'rgba':c} for n,c in [('Factory',list(color)),('Pearl',[.7,.74,.77,1]),('Obsidian',[.025,.03,.04,1]),('Mint',[.08,.55,.33,1]),('Indigo',[.10,.12,.44,1]),('Sand',[.60,.48,.28,1])]],'livery_recipes':[{'id':'LIVERY_01','description':'dual centre stripes','status':'not_generated'},{'id':'LIVERY_02','description':'diagonal racing panels','status':'not_generated'},{'id':'LIVERY_03','description':'lower-body speedline','status':'not_generated'}],'provenance':{'method':'original procedural Blender geometry','generator':'tools/generate_vehicles.py','external_inputs':[]},'limitations':['No baked UV textures; scalar PBR materials only','No damage morphs or mask textures','Paint presets are metadata; only factory paint exported','Livery recipes not generated','No Wicked import or gameplay testing yet','Collision proxy requires physics tuning']}
 (d/'vehicle.json').write_text(json.dumps(data,indent=2),encoding='utf8')
 for p in sorted(d.iterdir()):
  if p.is_file() and not p.name.endswith('.blend1'):records.append({'asset_id':f'{id}_{p.stem}','file':str(p.relative_to(ROOT)).replace('\\','/'),'bytes':p.stat().st_size,'sha256':sha(p),'status':'draft_exported','type':p.suffix.lstrip('.')})
 print('CAR_COMPLETE',id,counts,flush=True)
(OUT/'manifest_fragment.json').write_text(json.dumps({'schema_version':1,'generator':'tools/generate_vehicles.py','assets':records},indent=2),encoding='utf8')
print('VEHICLE_BATCH_COMPLETE',flush=True)
