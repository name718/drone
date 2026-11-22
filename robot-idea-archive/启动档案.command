#!/bin/zsh
set -e
cd -- "${0:A:h}"
if [[ ! -x .venv/bin/python ]]; then
 python3 -m venv .venv
 .venv/bin/pip install -r requirements.txt
fi
.venv/bin/python manage.py seed
configured=$(.venv/bin/python -c "from app import app; ctx=app.app_context(); ctx.push(); print(int(bool(app.db().execute('select 1 from users limit 1').fetchone())))")
if [[ "$configured" != 1 ]]; then .venv/bin/python manage.py init-admin; fi
echo '造物档案：http://127.0.0.1:5187/  ·  账号 admin  ·  Ctrl+C 停止'
open http://127.0.0.1:5187/
exec .venv/bin/gunicorn --bind 127.0.0.1:5187 --workers 1 --threads 4 app:app
