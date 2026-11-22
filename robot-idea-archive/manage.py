import argparse,getpass,secrets,sys
from app import app
from werkzeug.security import generate_password_hash
parser=argparse.ArgumentParser();parser.add_argument('action',choices=['init-admin','seed']);parser.add_argument('--stdin',action='store_true');a=parser.parse_args()
with app.app_context():
 db=app.db()
 if a.action=='init-admin':
  if db.execute('SELECT 1 FROM users').fetchone():sys.exit('Admin already configured; use the website to change password.')
  p=sys.stdin.readline().rstrip('\n') if a.stdin else getpass.getpass('Set admin password (minimum 10 characters): ')
  if not 10<=len(p)<=128:sys.exit('Password must be 10–128 characters')
  db.execute('INSERT INTO users(username,password_hash) VALUES(?,?)',('admin',generate_password_hash(p)));db.commit();print('Admin configured (password not echoed).')
 else:
  from seed import seed
  seed(app)
