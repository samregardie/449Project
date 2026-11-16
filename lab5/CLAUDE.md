# lab5/CLAUDE.md

Lab 5: Time Synchronization and Consensus (due 2026-11-18, 11:59pm). The spec is `../docs/handouts/Lab5 Handout.pdf`; re-read the relevant part before answering anything specific. The repo-wide rules in `../AGENTS.md` still apply (west workspace is upstream, no new scaffolding, AI-use policy).

## Current state of this folder

`lab5/` started as a copy of the finished Lab 3 code (commit `2daa8d7`); none of the Lab 5 work is in yet. A few comments and `pins.md` still say "Lab 3" or mention `stm32-can-test`, which was removed.

```
lab5/
  pi/proxy_receiver/   N1 cockpit (C, pigpio, SocketCAN on canA/canB). Payload layouts are in the receiver.c header comment.
  stm32-common/        Not an app. .c/.h shared by both STM32 nodes; each node's CMakeLists.txt adds them.
  stm32-front/         N2 steering app (NODE_ID=2): servo
  stm32-rear/          N3 drivetrain app (NODE_ID=3): drive (PI loop), motor, encoder
  pins.md              Every pin assignment for all three nodes; keep in sync with the overlays
```
What the inherited code does: `stm32-common/` has `canbus` (dedup by `(ID, seq)`, send on both buses), `ctrl` (CTRL CRC and range check) and `errorstate` (HB_TX, HB_MON, DEG/ERR, error state, test button B1/PC13, IWDG 100 ms). `blinker` follows the cockpit's BLINK frames. RELAY (`stm32-common/src/relay.c`, N2 and N3, fed by a `canbus` tap after dedup) forwards everything while a node has heard no HB from one of the other two zones on one bus and none from the other on the other bus for 30 ms. The Pi relays HB_N2/HB_N3/FAULT the same way in `can_rx`. N1's HB fault bit includes losing the wheel link.
- Common code must not reference node-only hardware or modules. Both overlays need the same node labels for shared hardware. Node differences go through `NODE_ID` or init arguments from each `main.c`.
- Both `app.overlay` files use the same CAN wiring: SPI1 on D3 (SCK), D5 (MISO), D4 (MOSI); CS A = D10, B = D9; INT A = D6, B = D7.

## What Lab 5 adds (handout summary)

All three nodes (cockpit N1, steering N2, drivetrain N3) run Raft. The log holds the network state; the leader is also the time master.

- **Per node (Part 1):** a Raft agent (consensus + state machine), its replicated log, a config file, and a Raft client that watches links, reports changes to the leader (a follower redirects it) and builds routes.
- **Config (Part 2), identical on every node:** `N` (3; majority N/2+1), `default_leader` (`cockpit`, or none; with a leader given, everyone boots in term 1 with no election), `heartbeat_ms` (20; also the sync period), `missed_heartbeats` (3), `election_timeout_jitter_ms` (39; timeout = missed × heartbeat + U(0, jitter)), `log_size` (30000), `nodes` (unique IDs), `Debug` (print election/append messages). Checkoff also uses `LEADER=random` with a seed.
- **Log (Part 3):** entries are (term, index, event). Events: `COCKPIT_OK/ERROR` (leave/enter failure mode), `NODE_X_UP/DOWN`, `CANLINK_X_Y_UP/DOWN` (6 links: 3 nodes × 2 buses).
- **State machine (Part 4):** `addLink(X,Y)`, `dropLink(X,Y)` (node and bus stay), `addNode(X)` (no links until addLink), `dropNode(X)` (removes its links), `getNetwork()` (the only read).
- **Routing (Part 5):** each node builds its routing table from `getNetwork()`; this replaces the Lab 3 timeout-based relay rule. Two-link failures may take several rounds to settle depending on who leads.
- **Time sync (Part 6):** the leader's heartbeat (REF) carries its clock. Follower clock = L₁ + network delay (CAN properties, TTCAN-style, not a round trip); set it with `clock_settime()`. Drift ratio = (L₂−L₁)/(F₂−F₁), applied between syncs. Provide `global_gettime()`, a periodic task printing the drift correction, and a PPS GPIO (rising edge every whole global second) that prints global time, local time, drift ratio and correction since the last heartbeat. Blinkers run off global time so front and rear line up.
- **Failure behavior:** node down → election, the new leader takes over REF, failure mode with hazards in step. CAN link down → no election; nodes stay reachable by relaying.

Message names the checkoff uses: REF (20 ms, cycle counter + master time), APPEND (every 100 ms, empty when idle), REQ_VOTE, MEMBER (one per link/node change), EVENT. IDs and payloads are the team's design; record them here and in `pi/proxy_receiver/receiver.c` once chosen.

New signals the checkoff scopes or looks at: LEADER LED, SYNC LED, REF_RX toggle, PPS, HZ, plus FL/RL as before. Add each to `pins.md` and the overlays.

## Checkoff sequence (abridged)

1. Cold start, `LEADER=cockpit`, proxy stopped: all in error, hazards in step (FL, RL, HZ), LEADER on Pi only, SYNC on all. Start proxy → leave error.
2. `candump -c any`: every ID, REF every 20 ms, empty APPEND every 100 ms, no REQ_VOTE.
3. Blinkers left/right/cancel; FL and RL within 1 ms over ten edges.
4. One node's A jumper, then B: DEG as Lab 3, term unchanged, one MEMBER per change at the same index everywhere.
5. Pi as master, N1 off B and N3 off A: still steers/blinks/brakes, FL/RL within 1 ms, term unchanged, two MEMBER entries, an EVENT from N2 reaches N3's log via the Pi.
6. Pi off: error and motor braked within 100 ms; within 1 s one STM32 LEADER, REF from it, hazards 4 Hz in step, no step > 100 µs at handover.
7. Pi back: rejoins as follower, held events shown exactly once, in order, marked replayed; hazards back to 2 Hz; LEADER stays on the STM32.
8. `LEADER=random`: kill the STM32 leader → new leader within 1 s, no failure mode; kill the Pi → failure mode, replay on return; then the seed where the Pi wins, and step 6 again.
9. STM32 master, scenario (c) jumpers cut the other STM32 off: SYNC stays on, REF_RX toggles, FL/RL within 1 ms; restore with no step.
10. Isolation and test button as Lab 3; term changes only if the isolated node was leader, exactly once.

Expect questions to everyone, including what changes with four nodes.

Report (1 page each): time-drift adjustment (when and how), the Raft state machine, the Raft client and routing during multi-link failures. Save PPS printouts and scope captures for it.

The team must explain every design decision, so explain and review rather than hand over a finished Raft agent, sync algorithm or routing logic (see the AI-use policy in `../AGENTS.md`).

## Commands

From the repo root, after `source ./setup449` (the aliases build `lab5/`):
```sh
west build-front / west flash-front   # N2 (add -p for a clean build; needed once after switching labs)
west build-rear  / west flash-rear    # N3 (both boards plugged in: add --dev-id <serial>)
```
Pi: `cd lab5/pi/proxy_receiver && sudo ./run.sh` (brings up both buses, builds, starts). Watch traffic with `candump -c -t d any`.

Pi bring-up (carried over from Lab 3): `dtparam=spi=on` and the two `mcp2515-can0`/`can1` overlays in `/boot/firmware/config.txt`. The kernel names CAN interfaces in probe order, so a udev rule names them by chip select. It lives only on the SD card; redo it after a re-flash:
```sh
sudo tee /etc/udev/rules.d/70-can-names.rules <<'EOF'
SUBSYSTEM=="net", ACTION=="add", KERNELS=="spi0.0", NAME="canA"
SUBSYSTEM=="net", ACTION=="add", KERNELS=="spi0.1", NAME="canB"
EOF
sudo reboot
```
Wiring notes: check the module crystal against `osc-freq` (Zephyr) and `oscillator=` (Pi); one 120 Ω at each end of each bus (60 Ω CANH–CANL powered off); common ground first; pulling a node's jumper is the fault injection.

Useful upstream references (read-only): `clock_settime`/`clock_gettime` in `../zephyr/include/zephyr/posix/posix_time.h` and `../zephyr/lib/os/clock.c`, `../zephyr/drivers/can/can_mcp2515.c`.
