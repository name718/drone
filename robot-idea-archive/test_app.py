import io,json,tempfile,unittest,zipfile
from pathlib import Path
from PIL import Image
from werkzeug.security import generate_password_hash
from app import create_app

class ArchiveTest(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.app=create_app({'TESTING':True,'DATA_DIR':self.tmp.name})
  with self.app.app_context():
   self.app.db().execute('INSERT INTO users(username,password_hash) VALUES(?,?)',('admin',generate_password_hash('local-test-password')));self.app.db().commit()
  self.c=self.app.test_client();self.csrf=self.c.get('/api/session').json['csrf']
 def tearDown(self):self.tmp.cleanup()
 def post(self,path,b=None,**kw):return self.c.post('/api'+path,json=b or {},headers={'X-CSRF-Token':self.csrf},**kw)
 def login(self):
  r=self.post('/login',dict(username='admin',password='local-test-password'));self.assertEqual(r.status_code,200);self.csrf=r.json['csrf']
 def idea(self):
  r=self.post('/ideas',dict(title='测试机器人',subtitle='单元测试',features=[dict(name='点头',state='已建模')],sensors=['IMU']))
  self.assertEqual(r.status_code,201);return r.json['idea']
 def test_access_and_csrf(self):
  self.assertEqual(self.c.get('/api/ideas').status_code,401)
  self.assertEqual(self.c.post('/api/login',json={}).status_code,403)
  self.assertEqual(self.post('/login',dict(username='admin',password='wrong')).status_code,401)
  self.login();self.assertEqual(self.c.get('/api/ideas').status_code,200)
  with self.c.get('/') as response:self.assertIn("default-src 'self'",response.headers['Content-Security-Policy'])
 def test_edit_audit_and_persistence(self):
  self.login();i=self.idea();b={k:i[k] for k in ['title','subtitle','description','category','status','version','features','sensors','next_steps']};b['title']='修订后';b['features'][0]['state']='待验证'
  r=self.c.put('/api/ideas/'+i['id'],json=b,headers={'X-CSRF-Token':self.csrf});self.assertEqual(r.status_code,200)
  events=r.json['idea']['events'];self.assertTrue(any('测试机器人 → 修订后' in e['body'] for e in events))
  other=create_app({'TESTING':True,'DATA_DIR':self.tmp.name})
  with other.app_context():self.assertEqual(other.db().execute('SELECT title FROM ideas').fetchone()[0],'修订后')
 def test_reviews_history_and_validation(self):
  self.login();i=self.idea();b=dict(appearance=8,feasibility=6,value=9,simplicity=7,note='真实意见')
  r=self.post('/ideas/'+i['id']+'/reviews',b);self.assertEqual(r.status_code,201);self.assertEqual(r.json['idea']['latest_review']['score'],7.5)
  b['appearance']=10;r=self.post('/ideas/'+i['id']+'/reviews',b);self.assertEqual(len(r.json['idea']['reviews']),2)
  b['appearance']=11;self.assertEqual(self.post('/ideas/'+i['id']+'/reviews',b).status_code,400)
  b['appearance']='NaN';self.assertEqual(self.post('/ideas/'+i['id']+'/reviews',b).status_code,400)
  b.update(appearance=7,feasibility=6,value=9,simplicity=7)
  self.assertEqual(self.post('/ideas/'+i['id']+'/reviews',b).json['idea']['latest_review']['score'],7.3)
 def test_photo_private_export_and_no_exif(self):
  self.login();i=self.idea();buf=io.BytesIO();Image.new('RGB',(40,30),'red').save(buf,format='PNG');buf.seek(0)
  r=self.c.post('/api/ideas/'+i['id']+'/photos',data=dict(photo=(buf,'../../image.png'),caption='第一次试装',kind='photo',recorded_at='2026-10-25T12:30:00+08:00'),headers={'X-CSRF-Token':self.csrf})
  self.assertEqual(r.status_code,201);p=r.json['idea']['photos'][0]
  with self.c.get(p['url']) as response:self.assertEqual(response.status_code,200)
  with self.c.get('/api/export') as response:
   self.assertEqual(response.status_code,200)
   with zipfile.ZipFile(io.BytesIO(response.data)) as z:
    self.assertIn('media/'+p['filename'],z.namelist());self.assertIn('ideas.json',z.namelist())
  self.assertEqual(self.app.test_client().get(p['url']).status_code,401)
  self.assertEqual(self.c.post('/api/ideas/'+i['id']+'/photos',data={'photo':(io.BytesIO(b'<svg>bad</svg>'),'a.svg')},headers={'X-CSRF-Token':self.csrf}).status_code,400)
 def test_notes_dates_archive_and_password_sessions(self):
  self.login();i=self.idea()
  r=self.post('/ideas/'+i['id']+'/events',dict(title='试装反馈',body='小脚好看',event_at='2026-10-25T15:25:00+08:00'));self.assertEqual(r.status_code,201)
  self.assertEqual(self.post('/ideas/'+i['id']+'/events',dict(title='bad',event_at='bad')).status_code,400)
  self.assertTrue(self.post('/ideas/'+i['id']+'/archive').json['idea']['archived'])
  self.assertFalse(self.post('/ideas/'+i['id']+'/archive').json['idea']['archived'])
  other=self.app.test_client();token=other.get('/api/session').json['csrf'];other.post('/api/login',json=dict(username='admin',password='local-test-password'),headers={'X-CSRF-Token':token})
  self.assertEqual(self.post('/password',dict(current='local-test-password',password='changed-local-password')).status_code,200)
  self.assertEqual(other.get('/api/ideas').status_code,401)
  self.assertEqual(self.c.get('/api/ideas').status_code,200)
 def test_json_shape_limits_and_rate_limit(self):
  self.login()
  self.assertEqual(self.c.post('/api/ideas',json=['bad'],headers={'X-CSRF-Token':self.csrf}).status_code,400)
  self.assertEqual(self.post('/ideas',dict(title='')).status_code,400)
  for _ in range(8):self.post('/login',dict(username='admin',password='wrong'))
  self.assertEqual(self.post('/login',dict(username='admin',password='wrong')).status_code,429)
 def test_seed_idempotent_and_no_fake_user_scores(self):
  from seed import seed
  seed(self.app);seed(self.app)
  with self.app.app_context():
   self.assertEqual(self.app.db().execute('SELECT COUNT(*) FROM ideas').fetchone()[0],13)
   self.assertEqual(self.app.db().execute('SELECT COUNT(*) FROM media').fetchone()[0],36)
   self.assertEqual(self.app.db().execute('SELECT COUNT(*) FROM attachments').fetchone()[0],9)
   self.assertEqual(self.app.db().execute('SELECT COUNT(*) FROM reviews').fetchone()[0],0)
 def test_model_download_private_and_export(self):
  self.login();i=self.idea();name='owned-test.step';payload=b'ISO-10303-21; TEST MODEL END-ISO-10303-21;'
  (Path(self.tmp.name)/'model_files'/name).write_bytes(payload)
  with self.app.app_context():
   self.app.db().execute('INSERT INTO attachments VALUES(?,?,?,?,?,?,?)',('fixture-file',i['id'],name,'测试模型','step',len(payload),'2026-10-25T12:00:00+08:00'));self.app.db().commit()
  d=self.c.get('/api/ideas').json['ideas'][0];self.assertEqual(d['files'][0]['url'],'/files/'+name)
  with self.c.get('/files/'+name) as response:
   self.assertEqual(response.status_code,200);self.assertEqual(response.data,payload);self.assertIn('attachment',response.headers['Content-Disposition'])
  self.assertEqual(self.app.test_client().get('/files/'+name).status_code,401)
  self.assertEqual(self.c.get('/files/missing.step').status_code,404)
  with self.c.get('/api/export') as response:
   with zipfile.ZipFile(io.BytesIO(response.data)) as z:self.assertEqual(z.read('models/'+name),payload)

if __name__=='__main__':unittest.main(verbosity=2)
