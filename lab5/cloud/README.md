# Cloud mirror (Lab 5 extra credit)

A Lightsail VM runs Redis and a password-protected status page. The Pi publishes committed
log entries and a heartbeat to Redis over Tailscale; Tailscale Funnel serves the page over HTTPS.

| File | What it is |
| --- | --- |
| `app.py` | The server: login, `/` serves the page, `/state` returns fresh data from Redis as JSON |
| `page.html` | The page; it asks `/state` every second and updates in place |
| `setup.sh` | Installs and (re)deploys everything on the VM |
| `vehicle-gui.service` | Runs the page on localhost:8000 |
| `env.example` | Template for `/etc/vehicle-gui.env` (the secrets, never committed) |

Deploy or update, from your laptop on the tailnet:
```sh
scp -r lab5/cloud ubuntu@449-car:
ssh ubuntu@449-car 'sudo sh cloud/setup.sh'
```
First time only: create `/etc/vehicle-gui.env` on the VM from `env.example`.

## Pi side

The publisher is `lab5/pi/proxy_receiver/publisher.c`. One-time setup on the Pi:
```sh
sudo apt install -y libhiredis-dev
sudo nano /etc/vehicle-pub.env      # REDIS_PASSWORD=<the Redis password>
sudo chmod 600 /etc/vehicle-pub.env
```
Then `sudo ./run.sh` as usual; it reads that file. Look for `[N1] PUB connected to 449-car`.
No file means the publisher is off and the cockpit runs as before.

A fresh run (all nodes rebooted, log back at index 1) clears the old stream by itself:
the Pi prints `[N1] PUB new run: cleared the old stream`. One exception: with
`LEADER=random`, if the Pi starts well after the STM32s, clear it by hand on the VM with
`redis-cli --askpass DEL vehicle:log`.
