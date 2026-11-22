"""Copy only packaged website files; retain runtime data and any user-edited source."""
from pathlib import Path
from datetime import datetime
import hashlib,json,shutil

src=Path('/private/tmp/robot_archive_release_20261025_v2/创意档案网站')
dst=Path('/Users/mjt/Library/CloudStorage/OneDrive-Office365/sharp3d/创意档案网站')
assert src.is_dir() and dst.is_dir()
old_manifest=json.loads((dst/'SHA256清单.json').read_text())
expected={q['file']:q['sha256'] for q in old_manifest}
old_zip=Path('/private/tmp/robot_archive_release_20261025/创意档案网站/网站部署包.zip')
if old_zip.exists():expected['网站部署包.zip']=hashlib.sha256(old_zip.read_bytes()).hexdigest()
backups=[];count=0
backup=dst/('更新前用户改动备份_'+datetime.now().strftime('%Y%m%d-%H%M%S'))
for p in sorted(src.rglob('*')):
 if not p.is_file():continue
 rel=p.relative_to(src)
 assert not any(s in rel.parts for s in ('data','.venv','__pycache__'))
 q=dst/rel
 if q.exists() and str(rel) in expected:
  actual=hashlib.sha256(q.read_bytes()).hexdigest()
  if actual!=expected[str(rel)] and actual!=hashlib.sha256(p.read_bytes()).hexdigest():
   b=backup/rel;b.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(q,b);backups.append(str(rel))
 q.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,q);count+=1
for p in src.rglob('*'):
 if p.is_file():assert hashlib.sha256(p.read_bytes()).digest()==hashlib.sha256((dst/p.relative_to(src)).read_bytes()).digest()
print(json.dumps(dict(files_verified=count,user_edits_preserved=backups,runtime_data_untouched=True,destination=str(dst)),ensure_ascii=False))

# Delete only this task's exact, hash-verified browser download fixture.
download=Path('/Users/mjt/Downloads/潮汐 · 天气小站 · 完整装配 · 可编辑 STEP.step')
original=src/'seed_files/tide-station-full.step'
if download.exists():
 assert hashlib.sha256(download.read_bytes()).digest()==hashlib.sha256(original.read_bytes()).digest()
 download.unlink();print('Removed owned model-download QA fixture after byte-for-byte verification.')
