"""Vehicle mirror: one password-protected page showing what the Pi published to Redis."""
import html
import os
import secrets

import redis
from fastapi import Depends, FastAPI, HTTPException
from fastapi.responses import HTMLResponse
from fastapi.security import HTTPBasic, HTTPBasicCredentials

basic = HTTPBasic()
r = redis.Redis(password=os.environ["REDIS_PASSWORD"], decode_responses=True)


def login(c: HTTPBasicCredentials = Depends(basic)):
    ok_user = secrets.compare_digest(c.username, os.environ["GUI_USER"])
    ok_pass = secrets.compare_digest(c.password, os.environ["GUI_PASSWORD"])
    if not (ok_user and ok_pass):
        raise HTTPException(401, headers={"WWW-Authenticate": "Basic"})


app = FastAPI(dependencies=[Depends(login)])  # every route needs the login


@app.get("/", response_class=HTMLResponse)
def page():
    hb = r.hgetall("vehicle:heartbeat")  # {} once the key has expired: the Pi went quiet
    log = r.xrevrange("vehicle:log", count=20)  # newest first
    banner = "" if hb else "<p class='stale'>STALE: no heartbeat from the Pi</p>"
    # TODO (team): the five views: nodes alive/dead, bus A/B, leader, time master, held events
    rows = "".join(
        f"<tr><td>{html.escape(entry_id)}</td><td>{html.escape(str(fields))}</td></tr>"
        for entry_id, fields in log
    )
    # The refresh tag reloads the page every second (and re-sends the login), so no JavaScript.
    return f"""<!doctype html>
<meta http-equiv="refresh" content="1">
<title>Vehicle mirror</title>
<style>.stale {{ background: #fc0; padding: 4px; }}</style>
{banner}
<h2>Heartbeat</h2><pre>{html.escape(str(hb))}</pre>
<h2>Latest log entries</h2><table>{rows}</table>
"""
