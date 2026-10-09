"""Inventory actual local deliverables, validate structure, build an offline review page."""
from pathlib import Path
import hashlib, json, struct, math, wave, xml.etree.ElementTree as ET, html, re

ROOT=Path(__file__).resolve().parents[1]
ASSETS=ROOT/'assets'
SUFFIXES={'.glb','.blend','.wav','.svg','.png'}
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def glb_check(p):
 data=p.read_bytes()
 magic,version,total=struct.unpack_from('<4sII',data)
 assert magic==b'glTF' and version==2 and total==len(data),'GLB header'
 pos=12;doc=None;binary=b''
 while pos<len(data):
  length,kind=struct.unpack_from('<II',data,pos);pos+=8
  assert pos+length<=len(data),'GLB chunk bounds'
  chunk=data[pos:pos+length];pos+=length
  if kind==0x4e4f534a:doc=json.loads(chunk)
  elif kind==0x004e4942:binary=chunk
 assert doc is not None,'Missing JSON chunk'
 for b in doc.get('buffers',[]):
  assert not b.get('uri'),'External buffer in self-contained GLB'
  assert b['byteLength']<=len(binary),'Short binary buffer'
 for view in doc.get('bufferViews',[]):
  assert view.get('byteOffset',0)+view['byteLength']<=len(binary),'bufferView out of range'
 meshes=doc.get('meshes',[]);tris=0
 for m in meshes:
  for prim in m.get('primitives',[]):
   acc=doc['accessors'][prim['attributes']['POSITION']]
   assert acc['componentType']==5126 and acc['type']=='VEC3','Position format'
   v=doc['bufferViews'][acc['bufferView']];start=v.get('byteOffset',0)+acc.get('byteOffset',0);stride=v.get('byteStride',12)
   assert acc['count']>0,'Empty position accessor'
   assert start+(acc['count']-1)*stride+12<=v.get('byteOffset',0)+v['byteLength'],'Position accessor out of range'
   for i in range(acc['count']):assert all(math.isfinite(x) for x in struct.unpack_from('<3f',binary,start+i*stride)),'Nonfinite geometry'
   count=doc['accessors'][prim['indices']]['count'] if 'indices' in prim else acc['count']
   if prim.get('mode',4)==4:
    assert count%3==0,'Triangle index count'
    tris+=count//3
 assert meshes and tris>0,'No mesh geometry'
 return {'triangles':tris,'mesh_count':len(meshes),'material_count':len(doc.get('materials',[])),'node_count':len(doc.get('nodes',[]))}

def main():
 rows=[];failures=[]
 for p in sorted(ASSETS.rglob('*')):
  if not p.is_file() or p.suffix.lower() not in SUFFIXES:continue
  rel=p.relative_to(ROOT).as_posix();category=p.relative_to(ASSETS).parts[0]
  row={'asset_id':p.relative_to(ASSETS).as_posix().upper().replace('/','__'),'path':rel,'category':category,'format':p.suffix[1:],'bytes':p.stat().st_size,'sha256':digest(p),'status':'built','engine_integration':'not_evaluated_by_this_catalog'}
  try:
   if p.suffix=='.glb':
    row['checks']=glb_check(p)
    lod_match=re.search(r'(?:^|_)lod([0-3])$',p.stem,re.I)
    if category in ['vehicles','vehicles_realistic','modern_coupes','modern_germans'] and lod_match:
     lod=int(lod_match[1]);assert row['checks']['triangles']<=[60000,30000,12000,4000][lod],'Vehicle triangle budget'
     assert row['checks']['material_count']<=8,'Vehicle material budget'
   elif p.suffix=='.wav':
    with wave.open(str(p),'rb') as f:
     row['checks']={'sample_rate':f.getframerate(),'channels':f.getnchannels(),'sample_width':f.getsampwidth(),'frames':f.getnframes()}
     assert f.getframerate()==48000 and f.getnchannels() in (1,2) and f.getsampwidth()==3 and f.getnframes()>0,'Audio format'
   elif p.suffix=='.svg':assert ET.parse(p).getroot().tag.endswith('svg'),'SVG root'
   elif p.suffix=='.png':assert p.read_bytes()[:8]==b'\x89PNG\r\n\x1a\n','PNG signature'
   elif p.suffix=='.blend':
    assert p.read_bytes()[:7]==b'BLENDER' or p.read_bytes()[:4] in (b'\x28\xb5\x2f\xfd',b'\x1f\x8b\x08\x00'),'Blend file signature'
   row['structural_validation']='passed'
  except Exception as e:
   row['structural_validation']='failed';row['error']=str(e);failures.append({'path':rel,'error':str(e)})
  rows.append(row)
 counts={cat:sum(r['category']==cat for r in rows) for cat in sorted({r['category']for r in rows})}
 catalog={'schema_version':1,'scope':'golden_slice_procedural_drafts','assets':rows,'counts':counts,'not_certified':['Photorealism','Manufacturer CAD accuracy','Full game integration','60FPS target','All 16 cars / 12 layouts completion']}
 (ASSETS/'catalog.json').write_text(json.dumps(catalog,indent=2),encoding='utf8')
 report={'passed':not failures,'files_checked':len(rows),'counts':counts,'failures':failures,'limits':'Structural checks only. No perceptual audio, artistic signoff, full glTF spec conformance or runtime frame-time certification.'}
 (ASSETS/'validation_summary.json').write_text(json.dumps(report,indent=2),encoding='utf8')
 build_review(rows,counts,failures)
 print(json.dumps(report,indent=2))
 return bool(failures)

def build_review(rows,counts,failures):
 def url(path):return '../'+path
 def photo(path,title,note=''):
  return f'<article><a href="{url(path)}"><img loading="lazy" src="{url(path)}" alt="{html.escape(title)}"></a><h3>{html.escape(title)}</h3><p>{html.escape(note)}</p></article>'
 sections=[]
 models=[r for r in rows if r['format']=='glb' and 'collision' not in r['path'].lower() and not re.search(r'(?:^|_)lod[1-3]$',Path(r['path']).stem,re.I) and r['category'] not in ['fixtures']]
 if models:
  options=''.join(f'<option value="{url(r["path"])}">{html.escape(r["path"])}</option>'for r in models)
  sections.append('<section><h2>İnteraktiv 3D baxış</h2><p>Siçanla fırlat, təkərlə yaxınlaşdır. Modeli aşağıdakı siyahıdan seç. Bu pəncərə faktiki GLB faylını göstərir.</p><select id="model-select">'+options+'</select><model-viewer id="viewer" src="'+url(models[0]['path'])+'" camera-controls touch-action="pan-y" shadow-intensity="1" exposure="1.2" camera-orbit="135deg 70deg 105%" environment-image="neutral" loading="eager" style="width:100%;height:560px;background:radial-gradient(ellipse at center,#34454e,#131d25);border-radius:16px"></model-viewer><p id="model-status">3D model yüklənir…</p></section>')
 for cat,title in [('vehicles_realistic','Seçilmiş real modellər'),('modern_coupes','Müasir kupelər'),('modern_germans','Müasir BMW və Mercedes'),('vehicles','İlkin orijinal maşınlar'),('environment','Liman assetləri'),('tracks','Dock Loop')]:
  images=[r for r in rows if r['category']==cat and r['format']=='png']
  if images:sections.append('<section><h2>'+title+'</h2><div class="grid">'+''.join(photo(r['path'],(Path(r['path']).parent.name+' / '+Path(r['path']).stem).replace('_',' '),'Blender renderi • faktiki 3D model')for r in images)+'</div></section>')
 icons=[r for r in rows if r['category']=='ui' and '/abilities/' in r['path'] and '_color' in r['path']]
 sections.append('<section><h2>Qabiliyyət ikonları</h2><div class="icons">'+''.join(f'<a href="{url(r["path"])}"><img src="{url(r["path"])}" alt="{html.escape(Path(r["path"]).stem)}"><span>{Path(r["path"]).stem.split("_")[1]}</span></a>'for r in icons)+'</div></section>')
 sounds=[r for r in rows if r['format']=='wav' and any(k in r['path'].lower()for k in ['preview','montage'])]
 if not sounds:sounds=[r for r in rows if r['format']=='wav'][:4]
 sections.append('<section><h2>Audio dinləmə</h2><p>Prosedural sintez. Real mühərrik qeydi deyil; texniki yoxlama dinləmə təsdiqini əvəz etmir.</p>'+''.join(f'<div class="sound"><strong>{html.escape(Path(r["path"]).stem)}</strong><audio controls preload="none" src="{url(r["path"])}"></audio></div>'for r in sounds)+'</section>')
 concepts=[r for r in rows if r['category']=='concepts' and r['format']=='png']
 sections.append('<section><h2>Vizual hədəf / konsept</h2><p>Aşağıdakı AI konsepti real 3D renderi və ya oyunun screenshot-u deyil.</p><div class="grid">'+''.join(photo(r['path'],'Harbor art direction','AI konsept referansı')for r in concepts)+'</div></section>')
 links=''.join(f'<tr><td>{html.escape(r["category"])}</td><td><a href="{url(r["path"])}">{html.escape(r["path"])}</a></td><td>{r["format"]}</td><td>{r["bytes"]/1024:.1f} KB</td></tr>'for r in rows)
 sections.append('<section><h2>Bütün fayllar</h2><input id="search" placeholder="Model, format və ya fayl adı ilə axtar"><div class="table"><table><thead><tr><th>Kateqoriya</th><th>Fayl</th><th>Format</th><th>Ölçü</th></tr></thead><tbody>'+links+'</tbody></table></div></section>')
 document='''<!doctype html><html lang="az"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Combat Racer — Asset Review</title><style>
 :root{color-scheme:dark;font-family:Inter,Segoe UI,sans-serif;background:#0b1117;color:#e5eff4}*{box-sizing:border-box}body{margin:0}header,main,footer{max-width:1320px;margin:auto;padding:32px}header{padding-top:64px}small{letter-spacing:.2em;color:#65d9dd;text-transform:uppercase}h1{font-size:clamp(32px,5vw,66px);letter-spacing:-.04em;margin:18px 0}h2{font-size:27px;margin:0 0 20px}h3{font-size:17px;margin:18px 18px 8px}p{color:#9bb0bb;line-height:1.7;max-width:900px}.stats{display:flex;gap:12px;flex-wrap:wrap}.stat{border:1px solid #29404b;border-radius:12px;padding:16px 22px;background:#13212a}.stat b{display:block;font-size:28px}.stat span{color:#8ba6b2}section{margin:28px 0 58px}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:20px}article{background:#14212b;border:1px solid #263946;border-radius:16px;overflow:hidden}article img{width:100%;display:block;aspect-ratio:4/3;object-fit:contain;background:#1d2933}article p{margin:0 18px 22px;font-size:13px}.icons{display:grid;grid-template-columns:repeat(8,1fr);gap:12px}.icons a{display:grid;place-items:center;gap:12px;padding:24px 8px;background:#14212b;border-radius:12px;text-transform:capitalize}.icons img{width:55px;height:55px}a{color:#81dce5;text-decoration:none}a:hover{text-decoration:underline}.sound{display:flex;align-items:center;gap:24px;flex-wrap:wrap;padding:16px;background:#14212b;border-radius:12px;margin:12px 0}.sound strong{min-width:250px;font-size:14px}audio{width:min(500px,100%)}input{width:100%;padding:16px;border:1px solid #365261;border-radius:10px;background:#14212b;color:white;margin:0 0 16px}.table{overflow:auto}table{border-collapse:collapse;width:100%;font-size:13px}th,td{text-align:left;padding:12px;border-bottom:1px solid #233743}th{color:#8ba6b2}.note{padding:18px;border-left:3px solid #e5ae68;background:#211e19;border-radius:5px;color:#dec5a2}.ok{color:#85e2ad}@media(max-width:700px){header,main,footer{padding:22px}.icons{grid-template-columns:repeat(4,1fr)}}
 </style><header><small>COMBAT RACER / ASSET WORKSHOP</small><h1>İstehsal paketi 01</h1><p>Redaktə edilə bilən 3D mənbələr, GLB modellər, WAV səslər və SVG interfeys elementləri. Konsept, texniki hazır fayl və oyun daxilində təsdiqlənmiş nəticə burada ayrı göstərilir.</p><div class="stats">'''
 document+=''.join(f'<div class="stat"><b>{n}</b><span>{html.escape(cat)}</span></div>'for cat,n in counts.items())+'</div><p class="note">Bu paket ilkin istehsal assetləridir. Bədii keyfiyyət, collision tuning və tam oyun performansı üzrə iş qalır. Saylara LOD, mənbə və preview faylları da daxildir; bunlar ayrıca maşın sayı deyil.</p></header><main>'+''.join(sections)+'</main><footer><a href="../assets/catalog.json">Asset kataloqu</a> · <a href="../assets/validation_summary.json">Yoxlama hesabatı</a><p>Struktur yoxlaması: '+('KEÇDİ'if not failures else f'{len(failures)} xəta')+'''</p></footer><style>select{padding:16px;background:#14212b;color:#e5eff4;border:1px solid #365261;width:100%;border-radius:10px;margin-bottom:15px}</style><script type="module" src="vendor/model-viewer.min.js"></script><script>document.querySelector('#search').addEventListener('input',e=>{const q=e.target.value.toLowerCase();document.querySelectorAll('tbody tr').forEach(r=>r.hidden=!r.textContent.toLowerCase().includes(q))});const pick=document.querySelector('#model-select'),viewer=document.querySelector('#viewer'),status=document.querySelector('#model-status');if(pick){pick.addEventListener('change',()=>{viewer.src=pick.value;status.textContent='3D model yüklənir…'});viewer.addEventListener('load',()=>status.textContent='Faktiki GLB modeli yükləndi.');viewer.addEventListener('error',()=>status.textContent='3D yüklənmədi. Səhifəni lokal HTTP server ilə açın; fayl linkləri aşağıdadır.');}</script></html>'''
 (ROOT/'review').mkdir(exist_ok=True);(ROOT/'review/index.html').write_text(document,encoding='utf8')

if __name__=='__main__':raise SystemExit(main())
