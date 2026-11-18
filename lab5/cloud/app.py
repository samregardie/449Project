"""Vehicle mirror: one password-protected page showing what the Pi published to Redis.

`/` serves page.html once; the page then asks `/state` for fresh data every second
and updates itself in place, so nothing reloads.
"""
import os
import re
import secrets
import time
from pathlib import Path

import redis
from fastapi import Depends, FastAPI, HTTPException
from fastapi.responses import HTMLResponse
from fastapi.security import HTTPBasic, HTTPBasicCredentials

NODE_NAMES = {1: "Cockpit (Pi)", 2: "Steering", 3: "Drivetrain"}
LOG_ROWS = 15        # latest entries shown in the table
HELD_WINDOW = 1000   # how many recent entries we scan for replayed events
PAGE = (Path(__file__).parent / "page.html").read_text()

basic = HTTPBasic()
r = redis.Redis(password=os.environ["REDIS_PASSWORD"], decode_responses=True)

# The last heartbeat we saw. Kept so the page can still show the last known
# state, greyed out, after the Pi goes quiet and the Redis key expires.
last_heartbeat = {}
last_heartbeat_at = 0.0


def login(c: HTTPBasicCredentials = Depends(basic)):
    ok_user = secrets.compare_digest(c.username, os.environ["GUI_USER"])
    ok_pass = secrets.compare_digest(c.password, os.environ["GUI_PASSWORD"])
    if not (ok_user and ok_pass):
        raise HTTPException(401, headers={"WWW-Authenticate": "Basic"})


app = FastAPI(dependencies=[Depends(login)])  # every route needs the login


def parse_network(text):
    """'N1=up(A,B) N2=down N3=up(-,B) cockpit=ok' -> list of nodes, cockpit state."""
    nodes = []
    for node, state in re.findall(r"N(\d)=(\S+)", text):
        up = state.startswith("up")
        nodes.append({
            "id": int(node),
            "name": NODE_NAMES.get(int(node), f"Node {node}"),
            "up": up,
            "bus_a": up and "A" in state,
            "bus_b": up and "B" in state,
        })
    cockpit = re.search(r"cockpit=(\w+)", text)
    return nodes, cockpit.group(1) if cockpit else "unknown"


@app.get("/", response_class=HTMLResponse)
def page():
    return PAGE


@app.get("/state")
def state():
    global last_heartbeat, last_heartbeat_at
    hb = r.hgetall("vehicle:heartbeat")  # {} once the key has expired: the Pi went quiet
    if hb:
        last_heartbeat, last_heartbeat_at = hb, time.time()

    entries = [fields for _id, fields in r.xrevrange("vehicle:log", count=HELD_WINDOW)]
    # Held events: EVENTs the leader committed while the Pi was away, which reach
    # us marked replayed once the Pi is back
    held = sum(1 for e in entries if e.get("type") == "EVENT" and e.get("replayed") == "1")
    nodes, cockpit = parse_network(last_heartbeat.get("network", ""))

    return {
        "stale": not hb,
        "seconds_since_heartbeat": round(time.time() - last_heartbeat_at) if last_heartbeat_at else None,
        "nodes": nodes,
        "cockpit": cockpit,
        "leader": int(last_heartbeat.get("leader", 0)),  # also the time master; 0 = unknown
        "term": int(entries[0]["term"]) if entries else None,
        "global_us": int(last_heartbeat["global_us"]) if last_heartbeat else None,
        "held_events": held,
        "log": entries[:LOG_ROWS],
    }
