#!/bin/sh
# VM setup and redeploy: Redis (localhost only, with a password), the status page as a
# service, Redis forwarded to the tailnet, the page published with Funnel.
# Run on the VM with: sudo sh setup.sh   (safe to re-run after changing app.py)
set -e
cd "$(dirname "$0")"

ENV=/etc/vehicle-gui.env
[ -f $ENV ] || { echo "Create $ENV from env.example first"; exit 1; }
chmod 600 $ENV
. $ENV

apt-get update -q
apt-get install -y -q redis-server python3-venv

# Ubuntu's Redis already listens on localhost only; set the password (replacing any old one).
sed -i '/^requirepass /d' /etc/redis/redis.conf
echo "requirepass $REDIS_PASSWORD" >> /etc/redis/redis.conf
systemctl restart redis-server

mkdir -p /opt/vehicle-gui
cp app.py page.html requirements.txt /opt/vehicle-gui/
[ -d /opt/vehicle-gui/venv ] || python3 -m venv /opt/vehicle-gui/venv
/opt/vehicle-gui/venv/bin/pip install -q -r /opt/vehicle-gui/requirements.txt
cp vehicle-gui.service /etc/systemd/system/
systemctl daemon-reload
systemctl enable vehicle-gui
systemctl restart vehicle-gui

tailscale serve --bg --tcp 6379 tcp://localhost:6379 # Redis: tailnet only
tailscale funnel --bg 8000                            # page: public HTTPS
tailscale funnel status
