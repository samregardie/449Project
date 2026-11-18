# Cloud mirror (Lab 5 extra credit)

A Lightsail VM runs Redis and a password-protected status page. The Pi publishes committed
log entries and a heartbeat to Redis over Tailscale; Tailscale Funnel serves the page over HTTPS.

| File | What it is |
| --- | --- |
| `app.py` | The whole status page: login, then one route that reads Redis |
| `setup.sh` | Installs and (re)deploys everything on the VM |
| `vehicle-gui.service` | Runs the page on localhost:8000 |
| `env.example` | Template for `/etc/vehicle-gui.env` (the secrets, never committed) |

Deploy or update, from your laptop on the tailnet:
```sh
scp -r lab5/cloud ubuntu@vehicle-mirror:
ssh ubuntu@vehicle-mirror 'sudo sh cloud/setup.sh'
```
First time only: create `/etc/vehicle-gui.env` on the VM from `env.example`.
