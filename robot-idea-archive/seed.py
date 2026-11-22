from pathlib import Path
import json,shutil,secrets

def seed(app):
 root=Path(__file__).parent
 with app.app_context():
  db=app.db();count=0
  for item in json.loads((root/'seed.json').read_text()):
   if db.execute('SELECT 1 FROM ideas WHERE id=?',(item['id'],)).fetchone():continue
   fields={k:v for k,v in item.items() if k not in ('photos','events','files')}
   for k in ('features','sensors'):fields[k]=json.dumps(fields[k],ensure_ascii=False)
   fields['archived']=int(fields['archived'])
   db.execute('INSERT INTO ideas('+','.join(fields)+') VALUES('+','.join('?' for _ in fields)+')',tuple(fields.values()))
   for p in item['photos']:
    shutil.copyfile(root/'seed_assets'/p['filename'],Path(app.config['DATA_DIR'])/'uploads'/p['filename'])
    db.execute('INSERT INTO media VALUES(?,?,?,?,?,?,?)',(p['id'],item['id'],p['filename'],p['kind'],p['caption'],p['recorded_at'],p['created_at']))
   for ev in item['events']:
    db.execute('INSERT INTO events VALUES(?,?,?,?,?,?,?,?)',(secrets.token_hex(12),item['id'],'note',ev['title'],ev['body'],ev['date'],ev['date'],ev.get('source','import')))
   for f in item.get('files',[]):
    shutil.copyfile(root/'seed_files'/f['filename'],Path(app.config['DATA_DIR'])/'model_files'/f['filename'])
    db.execute('INSERT INTO attachments VALUES(?,?,?,?,?,?,?)',(f['id'],item['id'],f['filename'],f['label'],f['kind'],f['bytes'],f['created_at']))
   count+=1
  db.commit();print(f'Seeded {count} new ideas; existing records preserved.')
