"""Consistent SQLite + registered media backup; run under the app account."""
from pathlib import Path
from datetime import datetime,timezone,timedelta
import os,sqlite3,tempfile,zipfile,json,sys
data=Path(os.environ.get('ARCHIVE_DATA','/var/lib/robot-idea-archive'))
dest=Path(sys.argv[1]) if len(sys.argv)>1 else data/'backups'
dest.mkdir(parents=True,exist_ok=True,mode=0o700)
stamp=datetime.now(timezone(timedelta(hours=8))).strftime('%Y%m%d-%H%M%S')
out=dest/f'robot-archive-{stamp}.zip'
with tempfile.TemporaryDirectory() as tmp:
 snapshot=Path(tmp)/'archive.sqlite3'
 with sqlite3.connect(data/'archive.sqlite3') as src,sqlite3.connect(snapshot) as target:src.backup(target)
 with sqlite3.connect(snapshot) as con:
  files=[r[0] for r in con.execute('SELECT filename FROM media')]
  models=[r[0] for r in con.execute('SELECT filename FROM attachments')]
 with zipfile.ZipFile(out,'w',zipfile.ZIP_DEFLATED) as z:
  z.write(snapshot,'archive.sqlite3')
  for name in files:z.write(data/'uploads'/name,'uploads/'+name)
  for name in models:z.write(data/'model_files'/name,'model_files/'+name)
os.chmod(out,0o600)
print(out)
