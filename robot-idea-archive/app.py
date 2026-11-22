"""Private robot idea archive: SQLite records, protected media, review history."""
from __future__ import annotations
import io, json, os, secrets, sqlite3, time, zipfile, math
from pathlib import Path
from datetime import datetime, timedelta, timezone
from functools import wraps
from flask import Flask, request, session, jsonify, g, send_file, send_from_directory
from werkzeug.security import generate_password_hash, check_password_hash
from werkzeug.exceptions import HTTPException
from PIL import Image, ImageOps, UnidentifiedImageError

BASE=Path(__file__).resolve().parent
TZ=timezone(timedelta(hours=8))
STATUSES=('待评审','推进中','待验证','暂存','已完成')
FEATURE_STATES=('想法','已建模','待验证','已实现')
def now(): return datetime.now(TZ).isoformat(timespec='seconds')

def create_app(config=None):
 app=Flask(__name__,static_folder='static')
 data=Path(os.environ.get('ARCHIVE_DATA',BASE/'data'))
 app.config.update(DATA_DIR=str(data),MAX_CONTENT_LENGTH=22*1024*1024,
   SESSION_COOKIE_HTTPONLY=True,SESSION_COOKIE_SAMESITE='Lax',
   SESSION_COOKIE_SECURE=os.environ.get('ARCHIVE_HTTPS')=='1',
   PERMANENT_SESSION_LIFETIME=timedelta(days=7),TESTING=False)
 if config: app.config.update(config)
 data=Path(app.config['DATA_DIR']);data.mkdir(parents=True,exist_ok=True)
 (data/'uploads').mkdir(exist_ok=True)
 (data/'model_files').mkdir(exist_ok=True)
 secret=data/'session.key'
 if not secret.exists():
  try:
   fd=os.open(secret,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
   with os.fdopen(fd,'w') as f:f.write(secrets.token_hex(32))
  except FileExistsError:pass
 app.secret_key=secret.read_text()
 dbpath=data/'archive.sqlite3'
 def db():
  if 'db' not in g:
   g.db=sqlite3.connect(dbpath,timeout=20);g.db.row_factory=sqlite3.Row
   g.db.execute('PRAGMA foreign_keys=ON');g.db.execute('PRAGMA busy_timeout=20000')
  return g.db
 app.db=db
 @app.teardown_appcontext
 def close(_):
  if 'db' in g:g.pop('db').close()
 with app.app_context():
  db().executescript('''
   PRAGMA journal_mode=WAL;
   CREATE TABLE IF NOT EXISTS users(id INTEGER PRIMARY KEY,username TEXT UNIQUE NOT NULL,password_hash TEXT NOT NULL,session_version INTEGER NOT NULL DEFAULT 1);
   CREATE TABLE IF NOT EXISTS ideas(id TEXT PRIMARY KEY,title TEXT NOT NULL,subtitle TEXT NOT NULL DEFAULT '',description TEXT NOT NULL DEFAULT '',category TEXT NOT NULL DEFAULT '桌面机器人',status TEXT NOT NULL DEFAULT '待评审',version TEXT NOT NULL DEFAULT 'V1',created_at TEXT NOT NULL,updated_at TEXT NOT NULL,features TEXT NOT NULL DEFAULT '[]',sensors TEXT NOT NULL DEFAULT '[]',next_steps TEXT NOT NULL DEFAULT '',cover_id TEXT,source_note TEXT NOT NULL DEFAULT '',archived INTEGER NOT NULL DEFAULT 0);
   CREATE TABLE IF NOT EXISTS media(id TEXT PRIMARY KEY,idea_id TEXT NOT NULL REFERENCES ideas(id),filename TEXT NOT NULL UNIQUE,kind TEXT NOT NULL,caption TEXT NOT NULL,recorded_at TEXT NOT NULL,created_at TEXT NOT NULL);
   CREATE TABLE IF NOT EXISTS reviews(id TEXT PRIMARY KEY,idea_id TEXT NOT NULL REFERENCES ideas(id),reviewer TEXT NOT NULL,appearance REAL NOT NULL,feasibility REAL NOT NULL,value REAL NOT NULL,simplicity REAL NOT NULL,score REAL NOT NULL,note TEXT NOT NULL,created_at TEXT NOT NULL,source TEXT NOT NULL DEFAULT 'user');
   CREATE TABLE IF NOT EXISTS events(id TEXT PRIMARY KEY,idea_id TEXT NOT NULL REFERENCES ideas(id),kind TEXT NOT NULL,title TEXT NOT NULL,body TEXT NOT NULL,event_at TEXT NOT NULL,created_at TEXT NOT NULL,source TEXT NOT NULL DEFAULT 'user');
   CREATE TABLE IF NOT EXISTS attachments(id TEXT PRIMARY KEY,idea_id TEXT NOT NULL REFERENCES ideas(id),filename TEXT NOT NULL UNIQUE,label TEXT NOT NULL,kind TEXT NOT NULL,bytes INTEGER NOT NULL,created_at TEXT NOT NULL);
   CREATE TABLE IF NOT EXISTS login_attempts(ip TEXT PRIMARY KEY,failures INTEGER NOT NULL,started REAL NOT NULL);
  ''');db().commit()
 def fail(msg,status=400):return jsonify(error=msg),status
 def auth(fn):
  @wraps(fn)
  def inner(*args,**kwargs):
   user=db().execute('SELECT * FROM users WHERE id=?',(session.get('uid'),)).fetchone()
   if not user or session.get('sv')!=user['session_version']:return fail('请先登录',401)
   return fn(*args,**kwargs)
  return inner
 @app.before_request
 def guard():
  if request.path.startswith('/api/') and request.method in ('POST','PUT','PATCH','DELETE'):
   expected=session.get('csrf','')
   if not expected or not secrets.compare_digest(expected,request.headers.get('X-CSRF-Token','')):return fail('页面已过期，请刷新后重试',403)
   if request.is_json and not isinstance(request.get_json(),dict):return fail('请求内容需要是一个对象')
 @app.after_request
 def headers(resp):
  resp.headers['X-Content-Type-Options']='nosniff';resp.headers['X-Frame-Options']='DENY'
  resp.headers['Referrer-Policy']='same-origin'
  resp.headers['Content-Security-Policy']="default-src 'self'; img-src 'self' blob: data:; style-src 'self'; script-src 'self'; connect-src 'self'; frame-ancestors 'none'; object-src 'none'; base-uri 'self'; form-action 'self'"
  if request.path.startswith(('/api/','/media/','/files/')):resp.headers['Cache-Control']='no-store'
  return resp
 @app.errorhandler(HTTPException)
 def errors(exc):
  if request.path.startswith('/api/'):
   msg='上传内容超过 22 MB，请压缩后重试' if exc.code==413 else '请求格式不正确或记录不存在'
   return fail(msg,exc.code)
  return exc
 @app.get('/')
 def index():return send_from_directory(BASE/'static','index.html')
 @app.get('/healthz')
 def health():
  db().execute('SELECT 1').fetchone();return jsonify(ok=True)
 @app.get('/api/session')
 def get_session():
  session.setdefault('csrf',secrets.token_hex(24))
  u=db().execute('SELECT id,username,session_version FROM users WHERE id=?',(session.get('uid'),)).fetchone()
  return jsonify(authenticated=bool(u and session.get('sv')==u['session_version']),username=u['username'] if u else '',csrf=session['csrf'],configured=bool(db().execute('SELECT 1 FROM users LIMIT 1').fetchone()))
 @app.post('/api/login')
 def login():
  body=request.get_json() or {};ip=request.remote_addr or 'local';t=time.time()
  a=db().execute('SELECT * FROM login_attempts WHERE ip=?',(ip,)).fetchone()
  if a and t-a['started']<600 and a['failures']>=8:return fail('尝试次数较多，请 10 分钟后再试',429)
  username=str(body.get('username',''))[:100];password=str(body.get('password',''))[:512]
  u=db().execute('SELECT * FROM users WHERE username=?',(username,)).fetchone()
  if not u or not check_password_hash(u['password_hash'],password):
   count=a['failures']+1 if a and t-a['started']<600 else 1;start=a['started'] if a and t-a['started']<600 else t
   db().execute('INSERT OR REPLACE INTO login_attempts VALUES(?,?,?)',(ip,count,start));db().commit()
   return fail('账号或密码不正确',401)
  db().execute('DELETE FROM login_attempts WHERE ip=?',(ip,));db().commit()
  session.clear();session.update(uid=u['id'],sv=u['session_version'],csrf=secrets.token_hex(24));session.permanent=True
  return jsonify(ok=True,csrf=session['csrf'])
 @app.post('/api/logout')
 @auth
 def logout():session.clear();return jsonify(ok=True)
 @app.post('/api/password')
 @auth
 def password():
  b=request.get_json() or {};u=db().execute('SELECT * FROM users WHERE id=?',(session['uid'],)).fetchone()
  if not check_password_hash(u['password_hash'],str(b.get('current',''))):return fail('当前密码不正确')
  new=str(b.get('password',''))
  if not 10<=len(new)<=128:return fail('新密码需要 10–128 个字符')
  db().execute('UPDATE users SET password_hash=?,session_version=session_version+1 WHERE id=?',(generate_password_hash(new),u['id']));db().commit()
  session['sv']=u['session_version']+1
  return jsonify(ok=True)
 def idea_exists(i):
  return db().execute('SELECT * FROM ideas WHERE id=?',(i,)).fetchone()
 def event(i,kind,title,body='',when=None,source='user'):
  db().execute('INSERT INTO events VALUES(?,?,?,?,?,?,?,?)',(secrets.token_hex(12),i,kind,title,body,when or now(),now(),source))
 def full(i):
  row=idea_exists(i)
  if not row:return None
  out=dict(row);out['archived']=bool(out['archived'])
  for name in ('features','sensors'):out[name]=json.loads(out[name])
  for table,key,order in [('media','photos','recorded_at DESC,created_at DESC,rowid DESC'),('reviews','reviews','created_at DESC,rowid DESC'),('events','events','event_at DESC,created_at DESC,rowid DESC')]:
   out[key]=[dict(r) for r in db().execute(f'SELECT * FROM {table} WHERE idea_id=? ORDER BY {order}',(i,))]
  for p in out['photos']:p['url']='/media/'+p['filename']
  cover=next((p for p in out['photos'] if p['id']==out['cover_id']),None)
  if not cover:cover=next((p for p in out['photos'] if p['kind']!='animation'),None)
  out['cover']=cover['url'] if cover else None
  out['files']=[dict(r) for r in db().execute('SELECT * FROM attachments WHERE idea_id=? ORDER BY rowid',(i,))]
  for p in out['files']:p['url']='/files/'+p['filename']
  userreviews=[r for r in out['reviews'] if r['source']=='user']
  out['latest_review']=userreviews[0] if userreviews else (out['reviews'][0] if out['reviews'] else None)
  return out
 @app.get('/api/ideas')
 @auth
 def ideas():
  ids=[r['id'] for r in db().execute('SELECT id FROM ideas ORDER BY archived ASC,updated_at DESC')]
  return jsonify(ideas=[full(i) for i in ids],statuses=STATUSES,feature_states=FEATURE_STATES,server_time=now())
 def clean_idea(b):
  title=str(b.get('title','')).strip()
  if not title or len(title)>80:raise ValueError('名称需要 1–80 个字符')
  status=b.get('status','待评审')
  if status not in STATUSES:raise ValueError('请选择有效的进度状态')
  fs=b.get('features',[]);ss=b.get('sensors',[])
  if not isinstance(fs,list) or len(fs)>50 or not isinstance(ss,list) or len(ss)>40:raise ValueError('功能或传感器数量不正确')
  features=[]
  for f in fs:
   if not isinstance(f,dict):raise ValueError('功能格式不正确')
   name=str(f.get('name','')).strip()[:120];state=f.get('state','想法')
   if state not in FEATURE_STATES:raise ValueError('功能状态不正确')
   if name:features.append(dict(name=name,state=state))
  return dict(title=title,subtitle=str(b.get('subtitle','')).strip()[:180],description=str(b.get('description','')).strip()[:8000],category=str(b.get('category','桌面机器人')).strip()[:40] or '桌面机器人',status=status,version=str(b.get('version','V1')).strip()[:32] or 'V1',features=json.dumps(features,ensure_ascii=False),sensors=json.dumps([str(s).strip()[:80] for s in ss if str(s).strip()],ensure_ascii=False),next_steps=str(b.get('next_steps','')).strip()[:5000])
 @app.post('/api/ideas')
 @auth
 def add_idea():
  try:c=clean_idea(request.get_json() or {})
  except ValueError as e:return fail(str(e))
  i=secrets.token_hex(8);t=now();c.update(id=i,created_at=t,updated_at=t)
  cols=','.join(c);qs=','.join('?' for _ in c)
  db().execute(f'INSERT INTO ideas({cols}) VALUES({qs})',tuple(c.values()))
  event(i,'created','创建了新想法',c['subtitle']);db().commit();return jsonify(idea=full(i)),201
 @app.put('/api/ideas/<i>')
 @auth
 def edit_idea(i):
  old=idea_exists(i)
  if not old:return fail('找不到这个想法',404)
  try:c=clean_idea(request.get_json() or {})
  except ValueError as e:return fail(str(e))
  changed=[k for k,v in c.items() if old[k]!=v]
  if not changed:return jsonify(idea=full(i))
  c['updated_at']=now();db().execute('UPDATE ideas SET '+','.join(k+'=?' for k in c)+' WHERE id=?',(*c.values(),i))
  labels={'title':'名称','subtitle':'简介','description':'想法说明','category':'分类','status':'进度','version':'版本','features':'功能清单','sensors':'传感器','next_steps':'下一步'}
  lines=[]
  for k in changed:
   before=old[k];after=c[k]
   if k in ('features','sensors'):
    def describe(raw):
     items=json.loads(raw)
     return '、'.join(f"{v['name']}（{v['state']}）" if isinstance(v,dict) else v for v in items) or '空'
    before=describe(before);after=describe(after)
   lines.append(f"{labels[k]}：{str(before)[:1200]} → {str(after)[:1200]}")
  event(i,'updated','更新了'+ '、'.join(labels[k] for k in changed),'\n'.join(lines));db().commit();return jsonify(idea=full(i))
 @app.post('/api/ideas/<i>/archive')
 @auth
 def archive(i):
  old=idea_exists(i)
  if not old:return fail('找不到这个想法',404)
  archived=not bool(old['archived'])
  db().execute('UPDATE ideas SET archived=?,updated_at=? WHERE id=?',(int(archived),now(),i))
  event(i,'archived','收进暂存档案' if archived else '恢复到创意库');db().commit();return jsonify(idea=full(i))
 @app.post('/api/ideas/<i>/reviews')
 @auth
 def review(i):
  if not idea_exists(i):return fail('找不到这个想法',404)
  b=request.get_json() or {};vals=[]
  try:
   for key in ('appearance','feasibility','value','simplicity'):
    v=float(b[key])
    if not math.isfinite(v) or not 1<=v<=10:raise ValueError()
    vals.append(round(v,1))
  except (KeyError,TypeError,ValueError):return fail('四个评分都需要在 1–10 分之间')
  name=str(b.get('reviewer','我的评分')).strip()[:50] or '我的评分';note=str(b.get('note','')).strip()[:5000]
  score=math.floor(sum(vals)*10/4+0.5)/10;t=now()
  db().execute('INSERT INTO reviews VALUES(?,?,?,?,?,?,?,?,?,?,?)',(secrets.token_hex(12),i,name,*vals,score,note,t,'user'))
  event(i,'review','记录了一次评分',f'{name} · 综合 {score}/10\n{note}')
  db().execute('UPDATE ideas SET updated_at=? WHERE id=?',(t,i));db().commit();return jsonify(idea=full(i)),201
 def validated_date(v):
  try:
   dt=datetime.fromisoformat(str(v).replace('Z','+00:00'))
   if not dt.tzinfo:dt=dt.replace(tzinfo=TZ)
   return dt.astimezone(TZ).isoformat(timespec='seconds')
  except (ValueError,TypeError):raise ValueError('请填写有效日期和时间')
 @app.post('/api/ideas/<i>/events')
 @auth
 def note(i):
  if not idea_exists(i):return fail('找不到这个想法',404)
  b=request.get_json() or {};title=str(b.get('title','')).strip()[:120];body=str(b.get('body','')).strip()[:6000]
  if not title:return fail('请填写记录标题')
  try:when=validated_date(b.get('event_at') or now())
  except ValueError as e:return fail(str(e))
  event(i,'note',title,body,when);db().execute('UPDATE ideas SET updated_at=? WHERE id=?',(now(),i));db().commit();return jsonify(idea=full(i)),201
 @app.post('/api/ideas/<i>/photos')
 @auth
 def photo(i):
  if not idea_exists(i):return fail('找不到这个想法',404)
  f=request.files.get('photo')
  if not f:return fail('请选择照片')
  kind=request.form.get('kind','photo')
  if kind not in ('photo','render','screenshot','animation'):return fail('照片类型不正确')
  try:when=validated_date(request.form.get('recorded_at') or now())
  except ValueError as e:return fail(str(e))
  raw=f.read(20*1024*1024+1)
  if len(raw)>20*1024*1024:return fail('单张照片请控制在 20 MB 以内')
  try:
   with Image.open(io.BytesIO(raw)) as im:
    if im.format not in ('JPEG','PNG','WEBP'):raise ValueError()
    if im.width*im.height>32_000_000:raise ValueError()
    im=ImageOps.exif_transpose(im).convert('RGB');im.thumbnail((2400,2400))
    mid=secrets.token_hex(12);filename=mid+'.jpg';im.save(data/'uploads'/filename,'JPEG',quality=90,optimize=True)
  except (UnidentifiedImageError,OSError,ValueError,Image.DecompressionBombError):return fail('请上传有效的 JPG、PNG 或 WebP 图片，像素不要超过 3200 万')
  caption=str(request.form.get('caption','')).strip()[:300] or f.filename[:150]
  db().execute('INSERT INTO media VALUES(?,?,?,?,?,?,?)',(mid,i,filename,kind,caption,when,now()))
  db().execute('UPDATE ideas SET cover_id=COALESCE(cover_id,?),updated_at=? WHERE id=?',(mid,now(),i))
  event(i,'photo','添加了一张照片',caption,when);db().commit();return jsonify(idea=full(i)),201
 @app.patch('/api/ideas/<i>/cover')
 @auth
 def cover(i):
  mid=(request.get_json() or {}).get('media_id')
  if not db().execute('SELECT 1 FROM media WHERE id=? AND idea_id=?',(mid,i)).fetchone():return fail('照片不存在',404)
  db().execute('UPDATE ideas SET cover_id=?,updated_at=? WHERE id=?',(mid,now(),i));event(i,'photo','更新了封面');db().commit();return jsonify(idea=full(i))
 @app.get('/media/<filename>')
 @auth
 def media(filename):
  if not db().execute('SELECT 1 FROM media WHERE filename=?',(filename,)).fetchone():return fail('照片不存在',404)
  return send_from_directory(data/'uploads',filename)
 @app.get('/api/export')
 @auth
 def export():
  records=[full(r['id']) for r in db().execute('SELECT id FROM ideas ORDER BY created_at')]
  buf=io.BytesIO()
  with zipfile.ZipFile(buf,'w',zipfile.ZIP_DEFLATED) as z:
   z.writestr('ideas.json',json.dumps(dict(format='robot-idea-archive',version=1,exported_at=now(),ideas=records),ensure_ascii=False,indent=2))
   z.writestr('说明.txt','创意档案备份：ideas.json 含功能、时间线、评分历史、照片与模型元数据；media/ 为图片，models/ 为已登记的模型文件。所有时间保存时区。渲染图不是实物照片。\n')
   for p in db().execute('SELECT filename FROM media'):
    f=data/'uploads'/p['filename']
    if f.is_file():z.write(f,'media/'+f.name)
   for p in db().execute('SELECT filename FROM attachments'):
    f=data/'model_files'/p['filename']
    if f.is_file():z.write(f,'models/'+f.name)
  buf.seek(0);return send_file(buf,mimetype='application/zip',as_attachment=True,download_name='robot-ideas-'+datetime.now(TZ).strftime('%Y%m%d-%H%M')+'.zip')
 @app.get('/files/<filename>')
 @auth
 def model_file(filename):
  row=db().execute('SELECT * FROM attachments WHERE filename=?',(filename,)).fetchone()
  if not row:return fail('模型文件不存在',404)
  return send_from_directory(data/'model_files',filename,as_attachment=True,download_name=row['label']+Path(filename).suffix)
 return app

app=create_app()
if __name__=='__main__':app.run(host='127.0.0.1',port=int(os.environ.get('PORT','5187')),debug=False)
