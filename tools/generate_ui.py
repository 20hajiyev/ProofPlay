"""Deterministic editable vector UI for the first asset batch. Python stdlib only."""
from pathlib import Path
import json, hashlib

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'assets/ui'
OUT.mkdir(parents=True,exist_ok=True)
ROWS=[]
PALETTE={'background':'#101820','panel':'#182630','text':'#e5f5f7','muted':'#8da9b2','focus':'#5ee7f0'}
ABILITIES=[
 ('P01','Surge','#52e6f4','<path d="M36 8 17 35h14l-4 21 22-31H35z"/>'),
 ('P02','Lance','#ee795f','<path d="m32 8 12 23-12 9-12-9z"/><path d="m23 40-5 14 14-8 14 8-5-14" fill="none"/>'),
 ('P03','Pulse','#c493ff','<circle cx="32" cy="32" r="7"/><path d="M18 18a20 20 0 0 0 0 28m28-28a20 20 0 0 1 0 28M10 10a31 31 0 0 0 0 44m44-44a31 31 0 0 1 0 44" fill="none"/>'),
 ('P04','Trap','#ffc46a','<path d="m32 11 21 21-21 21L11 32z" fill="none"/><circle cx="32" cy="32" r="6"/><path d="M32 3v8m29 21h-8M32 61v-8M3 32h8" fill="none"/>'),
 ('P05','Needle','#99e980','<path d="m12 45 8-30 5 3-8 30zm16 4 8-34 5 3-8 34zm16-4 8-26 5 3-8 26z" stroke="none"/>'),
 ('P06','Ward','#74bfff','<path d="m32 7 21 8v17c0 12-13 21-21 25-8-4-21-13-21-25V15z" fill="none"/><path d="m22 32 7 8 15-19" fill="none"/>'),
 ('P07','Mend','#6ee1b6','<path d="M25 10h14v15h15v14H39v15H25V39H10V25h15z" stroke="none"/>'),
 ('P08','Storm','#ffcfef','<path d="M8 30a10 10 0 0 1 9-15 16 16 0 0 1 29 1 10 10 0 0 1 7 17H11" fill="none"/><path d="m25 32-8 14h10l-4 12 18-22H30l5-4" stroke="none"/>'),
]
def svg(body,color='#e5f5f7',size=64):
 return f'<svg xmlns="http://www.w3.org/2000/svg" width="{size}" height="{size}" viewBox="0 0 64 64"><g fill="{color}" stroke="{color}" stroke-width="3" stroke-linecap="round" stroke-linejoin="round">{body}</g></svg>'
def save(aid,name,content,category='ui',**meta):
 p=OUT/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(content,encoding='utf-8')
 ROWS.append(dict(asset_id=aid,category=category,status='built',source_paths=['tools/generate_ui.py'],files=[p.relative_to(ROOT).as_posix()],sha256=hashlib.sha256(p.read_bytes()).hexdigest(),provenance='Original deterministic SVG geometry',engine_integration=False,**meta))
for pid,name,col,path in ABILITIES:
 for variant,color in [('color',col),('mono','#ffffff')]:
  save(f'UI_{pid}_{variant.upper()}',f'abilities/{pid}_{name.lower()}_{variant}.svg',svg(path,color),semantic=name,variant=variant,viewbox=[0,0,64,64])
for state in ['normal','hover','focus','pressed','disabled']:
 fill={'normal':'#182630','hover':'#203a47','focus':'#182630','pressed':'#355e6a','disabled':'#182026'}[state]
 border='#5ee7f0' if state in ['hover','focus'] else '#3b535d'
 outline='<rect x="2" y="2" width="316" height="60" rx="12" fill="none" stroke="#e5f5f7" stroke-width="2"/>' if state=='focus' else ''
 save('UI_BUTTON_'+state.upper(),f'controls/button_{state}.svg',f'<svg xmlns="http://www.w3.org/2000/svg" width="320" height="64" viewBox="0 0 320 64">{outline}<rect x="6" y="6" width="308" height="52" rx="8" fill="{fill}" stroke="{border}" stroke-width="2"/></svg>',viewbox=[0,0,320,64],text_baked=False)
hud={
 'position':'<path d="M18 10h28v44H18z" fill="none"/><path d="m26 26 7-7v27m-6 0h12" fill="none"/>',
 'lap':'<path d="M15 15h34v34H15z" fill="none"/><path d="m7 24 8-9 9 8" fill="none"/>',
 'timer':'<circle cx="32" cy="36" r="21" fill="none"/><path d="M24 6h16M32 6v9m0 9v13l10 7" fill="none"/>',
 'health':'<path d="M32 54 9 30C-2 14 19 3 32 19 45 3 66 14 55 30z" fill="none"/>',
 'warning':'<path d="m32 7 27 48H5z" fill="none"/><path d="M32 24v14"/><circle cx="32" cy="46" r="2"/>',
 'save':'<path d="M12 9h34l8 8v38H12z" fill="none"/><path d="M22 9v17h22V9M22 55V36h23v19" fill="none"/>',
 'medal':'<circle cx="32" cy="23" r="16" fill="none"/><path d="m20 37-5 20 17-9 17 9-5-20" fill="none"/>',
 'sound':'<path d="M8 25h11l14-12v38L19 39H8z" fill="none"/><path d="M42 22a15 15 0 0 1 0 20m7-28a26 26 0 0 1 0 36" fill="none"/>',
}
for name,path in hud.items():save('UI_HUD_'+name.upper(),f'hud/{name}.svg',svg(path))
for key in ['W','A','S','D','Q','R','F','C','1','2','3','ESC','TAB','SHIFT','CTRL','SPACE']:
 text=f'<svg xmlns="http://www.w3.org/2000/svg" width="96" height="64" viewBox="0 0 96 64"><rect x="3" y="3" width="90" height="58" rx="10" fill="#182630" stroke="#8da9b2" stroke-width="2"/><text x="48" y="39" text-anchor="middle" fill="#e5f5f7" font-family="Arial,sans-serif" font-size="18">{key}</text></svg>'
 save('UI_KEY_'+key,f'glyphs/key_{key.lower()}.svg',text,editable_text=True)
(OUT/'tokens.json').write_text(json.dumps({'palette':PALETTE,'spacing':[4,8,12,16,24,32],'abilities':[{'id':p,'name':n,'color':c}for p,n,c,_ in ABILITIES]},indent=2),encoding='utf-8')
(OUT/'manifest_fragment.json').write_text(json.dumps({'assets':ROWS},indent=2),encoding='utf-8')
print(f'Created {len(ROWS)} editable SVG assets')

