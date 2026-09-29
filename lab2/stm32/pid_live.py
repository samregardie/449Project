#!/usr/bin/env python3
"""
Live PID graph for main.c's pid_live(): last 10 s of target, velocity and duty.

    source ./setup449                  # for pyserial
    python3 lab2/stm32/pid_live.py
"""

import collections
import glob
import sys
import threading
import tkinter as tk

import serial

POINTS = 500		# 10 s at one line per 20 ms
VEL_MAX = 40		# ticks per 10 ms
DUTY_MAX = 1000

data = collections.deque(maxlen=POINTS)


def read(port):
    while True:
        try:
            target, vel, duty = (int(x) for x in port.readline().decode(errors="ignore").split(","))
        except ValueError:
            continue	# partial or non-data line
        data.append((target / 10, vel / 10, duty))


def draw():
    c.delete("all")
    w, h = c.winfo_width(), c.winfo_height()
    half = h // 2
    pts = list(data)

    def line(values, top, height, lo, hi, color, dash=None):
        if len(values) < 2:
            return
        xy = []
        for i, v in enumerate(values):
            xy += [i * w / (POINTS - 1), top + height - (v - lo) / (hi - lo) * height]
        c.create_line(*xy, fill=color, width=2, dash=dash)

    for top, lo, hi, label in ((0, 0, VEL_MAX, "velocity"), (half, -DUTY_MAX, DUTY_MAX, "duty")):
        for i in range(5):
            y = top + i * half / 4
            c.create_line(0, y, w, y, fill="#e5e5e5")
            c.create_text(4, y + 2, anchor="nw", fill="#888", text=f"{hi - (hi - lo) * i / 4:g}")
        c.create_text(w - 4, top + 4, anchor="ne", fill="#444", text=label)

    line([p[0] for p in pts], 0, half, 0, VEL_MAX, "#d9480f", dash=(6, 4))
    line([p[1] for p in pts], 0, half, 0, VEL_MAX, "#1971c2")
    line([p[2] for p in pts], half, half, -DUTY_MAX, DUTY_MAX, "#2f9e44")

    if pts:
        t, v, d = pts[-1]
        root.title(f"PID live   target {t:.1f}   velocity {v:.1f}   duty {d}")
    root.after(50, draw)


ports = glob.glob("/dev/tty.usbmodem*") + glob.glob("/dev/ttyACM*")
if not ports:
    sys.exit("No Nucleo serial port found")

threading.Thread(target=read, args=(serial.Serial(ports[0], 115200),), daemon=True).start()

root = tk.Tk()
c = tk.Canvas(root, width=900, height=500, bg="white")
c.pack(fill="both", expand=True)
draw()
root.mainloop()
