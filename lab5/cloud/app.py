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

# The last heartbeat we saw and when it changed (by this server's clock, so no
# clock comparison with the Pi). Kept so the page can still show the last known
# state after the Pi goes quiet and the Redis key expires.
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


def relays(nodes):
    """Same rule as network_relay() in raftlog.c: two live nodes that share no bus
    need a third node that is up on both buses to forward between them."""
    def share_bus(x, y):
        return (x["bus_a"] and y["bus_a"]) or (x["bus_b"] and y["bus_b"])

    result = []
    for i, x in enumerate(nodes):
        for y in nodes[i + 1:]:
            if x["up"] and y["up"] and not share_bus(x, y):
                via = [n["id"] for n in nodes
                       if n not in (x, y) and n["up"] and n["bus_a"] and n["bus_b"]]
                result.append({"between": [x["id"], y["id"]], "via": via})  # via [] = no path
    return result


@app.get("/", response_class=HTMLResponse)
def page():
    return PAGE


@app.get("/state")
def state():
    global last_heartbeat, last_heartbeat_at
    hb = r.hgetall("vehicle:heartbeat")  # {} once the key has expired: the Pi went quiet
    if hb and hb.get("wall_ms") != last_heartbeat.get("wall_ms"):
        last_heartbeat, last_heartbeat_at = hb, time.time()

    entries = [fields for _id, fields in r.xrevrange("vehicle:log", count=HELD_WINDOW)]
    # Held events: EVENTs the leader committed while the Pi was away, which reach
    # us marked replayed once the Pi is back
    held = sum(1 for e in entries if e.get("type") == "EVENT" and e.get("replayed") == "1")
    nodes, cockpit = parse_network(last_heartbeat.get("network", ""))

    return {
        "stale": not hb,
        "heartbeat_age_s": round(time.time() - last_heartbeat_at, 1) if last_heartbeat_at else None,
        "nodes": nodes,
        "relays": relays(nodes),
        "cockpit": cockpit,
        # Why the vehicle is in the error state, from the Pi's live view: ["N3 self-test", ...]
        "errors": [e for e in last_heartbeat.get("errors", "").split(", ") if e],
        "leader": int(last_heartbeat.get("leader", 0)),  # 0 = unknown
        "term": int(entries[0]["term"]) if entries else None,
        "global_us": int(last_heartbeat["global_us"]) if last_heartbeat else None,
        "held_events": held,
        "log_length": r.xlen("vehicle:log"),
        "log": entries[:LOG_ROWS],
    }
