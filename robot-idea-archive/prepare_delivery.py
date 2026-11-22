from pathlib import Path
from datetime import datetime,timezone,timedelta
import json,hashlib,zipfile

R=Path(__file__).resolve().parent
ideas=json.loads((R/'seed.json').read_text())
for i in ideas:
 for p in i['photos']:p.update(url='seed_assets/'+p['filename'],idea_id=i['id'])
 i['cover']='seed_assets/'+next(p['filename'] for p in i['photos'] if p['id']==i['cover_id'])
 i['reviews']=[];i['latest_review']=None
 i['files']=[f|dict(url='seed_files/'+f['filename']) for f in i.get('files',[])]
 i['events']=[dict(id=f"{i['id']}-event{n}",idea_id=i['id'],kind='note',title=e['title'],body=e['body'],event_at=e['date'],created_at=e['date'],source=e.get('source','import')) for n,e in reversed(list(enumerate(i['events'])))]
payload=dict(ideas=ideas,statuses=['待评审','推进中','待验证','暂存','已完成'],feature_states=['想法','已建模','待验证','已实现'])
(R/'preview-data.js').write_text('window.ARCHIVE_PREVIEW = '+json.dumps(payload,ensure_ascii=False)+';\n')
html=(R/'static/index.html').read_text().replace('href="/static/','href="static/').replace('src="/static/app.js"','src="preview-data.js" defer></script><script src="static/app.js"')
html=html.replace('<title>造物档案 · 机器人创意工作室</title>','<title>造物档案 · 离线只读预览</title>')
(R/'00_打开网站预览.html').write_text(html)
files=[]
for p in R.rglob('*'):
 rel=p.relative_to(R)
 if not p.is_file() or any(part in {'.venv','data','__pycache__','.git'} for part in rel.parts):continue
 if p.name in {'网站部署包.zip','SHA256清单.json','.deploy_known_hosts'} or p.suffix in {'.pyc','.log'}:continue
 files.append(p)
manifest=[dict(file=str(p.relative_to(R)),bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in sorted(files)]
(R/'SHA256清单.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2))
with zipfile.ZipFile(R/'网站部署包.zip','w',zipfile.ZIP_DEFLATED) as z:
 for p in files+[R/'SHA256清单.json']:z.write(p,str(p.relative_to(R)))
print(json.dumps(dict(files=len(files)+1,ideas=len(ideas),media=sum(len(i['photos']) for i in ideas),zip_MiB=round((R/'网站部署包.zip').stat().st_size/1048576,2)),ensure_ascii=False))
