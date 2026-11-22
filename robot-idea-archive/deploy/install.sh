#!/usr/bin/env bash
# Run only on the selected Ubuntu server, after SSH connectivity is restored.
# Installs this app under /opt/robot-idea-archive and an isolated systemd service.
set -euo pipefail
app_dir=/opt/robot-idea-archive
data_dir=/var/lib/robot-idea-archive
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ $EUID -ne 0 ]]; then echo 'Run this reviewed installer as root on the target server.'; exit 1; fi
if [[ -e /etc/systemd/system/robot-idea-archive.service || -e "$app_dir/app.py" ]]; then
 echo 'Existing installation found. Installer stops without overwriting it; use the documented update procedure.'; exit 1
fi
if ss -lnt | awk '{print $4}' | grep -Eq '(^|:)5187$'; then echo 'Port 5187 is already in use; choose an app-specific free port before installing.'; exit 1; fi
if ! command -v python3 >/dev/null || ! python3 -c 'import ensurepip' >/dev/null 2>&1 || ! command -v curl >/dev/null; then
 apt-get update
 DEBIAN_FRONTEND=noninteractive apt-get install -y python3 python3-venv curl
fi
if ! id robotarchive >/dev/null 2>&1; then useradd --system --user-group --home-dir "$data_dir" --shell /usr/sbin/nologin robotarchive; fi
install -d -m 0755 "$app_dir"
install -d -o robotarchive -g robotarchive -m 0700 "$data_dir"
cp "$script_dir/app.py" "$script_dir/manage.py" "$script_dir/seed.py" "$script_dir/seed.json" "$script_dir/requirements.txt" "$app_dir/"
cp -R "$script_dir/static" "$script_dir/seed_assets" "$app_dir/"
if [[ -d "$script_dir/seed_files" ]]; then cp -R "$script_dir/seed_files" "$app_dir/"; fi
cp -R "$script_dir/deploy" "$app_dir/"
python3 -m venv "$app_dir/.venv"
"$app_dir/.venv/bin/pip" install --disable-pip-version-check -r "$app_dir/requirements.txt"
runuser -u robotarchive -- env ARCHIVE_DATA="$data_dir" "$app_dir/.venv/bin/python" "$app_dir/manage.py" seed
echo 'Set a NEW website access password; do not reuse the server root password.'
runuser -u robotarchive -- env ARCHIVE_DATA="$data_dir" "$app_dir/.venv/bin/python" "$app_dir/manage.py" init-admin
install -m 0644 "$script_dir/deploy/robot-idea-archive.service" /etc/systemd/system/robot-idea-archive.service
systemctl daemon-reload
systemctl enable --now robot-idea-archive.service
curl --fail --silent http://127.0.0.1:5187/healthz
echo
echo 'Installed. Service listens on instance port 5187. Configure one approved NAT TCP mapping for website access.'
echo 'No SSH, firewall, existing web service, or other systemd unit was changed.'
