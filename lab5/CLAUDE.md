# lab5/CLAUDE.md

Lab 5: Time Synchronization and Consensus (due 2026-11-18, 11:59pm). The spec is `../docs/handouts/Lab5 Handout.pdf`; re-read the relevant part before answering anything specific. The repo-wide rules in `../AGENTS.md` still apply (west workspace is upstream, no new scaffolding, AI-use policy).

## Current state of this folder

`lab5/` started as a copy of the finished Lab 3 code (commit `2daa8d7`). Lab 5 so far: the Pi is the fixed leader (step 1). In `tick()` it sends REF every 20 ms and an empty APPEND every 100 ms, drives LEADER (GPIO16) and SYNC (GPIO20), has `global_gettime()` (monotonic + offset, 0 while it leads) and prints its config at boot. Log replication, leader side (Pi, step 3 of the plan): `send_append_locked()` sends the next entry for the follower furthest behind, skipping followers silent on both buses for 60 ms (else one unplugged node blocks commits); an empty APPEND is just the "next entry" past the end. It goes out every 100 ms, right after the leader appends, and right after an ACK that shows a follower behind. `on_append_ack()` acts on one copy per seq (ACKs arrive on both buses), only raises `match_index` on a successful ACK (every follower ACKs every APPEND, so an up-to-date one ACKs old resent entries with a low match; trusting that made it look behind and caused an APPEND flood on replay), and lowers it only on a failed ACK (rebooted follower). `advance_commit_locked()` commits the newest current-term entry held by a majority (the Pi counts itself). `print_commits()` runs last in `tick()`. Test key on the Pi: `e` + Enter appends EVENT N1.

Follower side (STM32, `stm32-common/src/raft.c`): `on_append()` in the CAN dispatch thread, under `raft_lock` (k_mutex): refuse an older term; check prev index/term, else drop the conflicting entry and report our last index; store the entry unless we have it (same index and term); commit up to min(leader commit, match); note the reply. It never sends: a send can block for ms on a dead bus, and the dispatch thread also timestamps REF. The `raft_thread` (prio 4, below HB) sends the latest reply as APPEND_ACK, then applies and prints COMMIT/NET. No elections yet; the Raft client (link changes, test-button EVENT) is next. The 60 KB log puts RAM at ~78 % (N2) / ~82 % (N3).

STM32 clock sync (`stm32-common/src/globaltime.c`, step 2): both STM32s follow REF from any node. Team decisions: timestamp in the REF handler; `REF_DELAY_US` is one measured constant (placeholder 270 = frame time until scoped); `sys_clock_settime()` at every REF, with `CONFIG_SYS_CLOCK_TICKS_PER_SEC=1000000` because the clock counts in ticks; drift ratio over ~1 s windows, applied in `global_gettime()` between REFs; the ratio starts at 1, a window is restarted (ratio kept) on the first REF, after sync loss or on a new REF ID, and a window more than 500 ppm from 1 is ignored (a late REF, not a crystal). The clock is set to the REF time plus the time the handler took since its timestamp. SYNC goes off after 3 missed REFs. PPS rises from a timer ISR and the low-priority thread prints `global local ratio corr step`; corr is the drift correction `global_gettime()` applies at the edge, so this line is also the handout's periodic drift print (6.3). The Pi has PPS too (GPIO23, `pps()` thread, same line; ratio 1 while it leads). LEADER (PA15) is in the overlays but not driven yet. Blinkers (`blinker.c`) run off global time: a lamp is on while (global / half period) is even; turn 1 Hz, error hazards (BLINK_HAZARD) 2 Hz, failure mode (no HB_N1 on either bus) 4 Hz. A timer ISR runs on every 125 ms boundary of global time. BLINK is state only; the Pi sends it on change and every 100 ms. HZ test point: not decided yet. A few comments and `pins.md` still say "Lab 3" or mention `stm32-can-test`, which was removed.

```
lab5/
  pi/proxy_receiver/   N1 cockpit (C, pigpio, SocketCAN on canA/canB). build.sh includes ../../stm32-common/include for the shared headers.
  stm32-common/        Not an app. .c/.h shared by both STM32 nodes; each node's CMakeLists.txt adds them.
                       include/catalog.h and include/raft_config.h are plain C, shared by all three nodes (the Pi too).
  stm32-front/         N2 steering app (NODE_ID=2): servo
  stm32-rear/          N3 drivetrain app (NODE_ID=3): drive (PI loop), motor, encoder
  pins.md              Every pin assignment for all three nodes; keep in sync with the overlays
```
What the inherited code does: `stm32-common/` has `canbus` (dedup by `(ID, seq)`, send on both buses), `ctrl` (CTRL CRC and range check) and `errorstate` (HB_TX, HB_MON, DEG/ERR, error state, test button B1/PC13, IWDG 100 ms). `blinker` takes its state from the cockpit's BLINK frames (timing from global time, see above). RELAY (`stm32-common/src/relay.c`, N2 and N3, fed by a `canbus` tap after dedup) forwards everything while a node has heard no HB from one of the other two zones on one bus and none from the other on the other bus for 30 ms. The Pi relays HB_N2/HB_N3/FAULT the same way in `can_rx`. N1's HB fault bit includes losing the wheel link.
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

## Message catalog (team design, decided)

The full table (IDs, senders, timing, byte layouts, FAULT/BLINK/CLIENT values) lives once, in `stm32-common/include/catalog.h`, which the STM32s (through `canbus.h`) and the Pi both include. The key choices:
- Every message more than one node can send has one ID per node, `base + n` with n = node ID 1–3. That includes REF and APPEND: during a handover two nodes can both think they lead, and a shared ID would collide on the bus and break the `(ID, seq)` dedup.
- Priority: CTRL (0x010) > REF_Nn (0x012–0x014) > REQ_VOTE/VOTE > FAULT > APPEND/ACK/CLIENT > HB > BLINK > FORCE. CTRL's 2 ms deadline wins; REF waits at most one frame (~270 µs), which bounds the extra sync error.
- REF = term, a 1-byte cycle counter, 5-byte leader time in µs. A new leader continues the cycle counter from the last REF it heard. The seq nibble stays dedup-only.
- APPEND carries one entry; entry 0 means none (the 100 ms keep-alive). Layout: term, prev index (2), prev term, entry term, commit distance, entry. The entry keeps its own term (older than the leader's after an election, needed for checkoff 7 replay); the commit index is sent as prev + 1 − distance, capped 255 back (too low is safe, just late). When an ACK says a follower is behind, send the next APPEND right away, not on the next tick (checkoff 7 replay).
- MEMBER (link/node changes) and EVENT are log entry kinds inside APPEND/CLIENT, not CAN IDs.
- CLIENT = kind (request / redirect / ok), target node, entry or leader ID, request number.

## Raft config (Part 2)

`stm32-common/include/raft_config.h`, one file for all three nodes: node IDs (`NODE_COCKPIT` 1, `NODE_STEERING` 2, `NODE_DRIVE` 3), `RAFT_N`, `RAFT_DEFAULT_LEADER` (`RAFT_NO_LEADER` = 0 for LEADER=random), `RAFT_SEED`, heartbeat 20 ms, 3 missed heartbeats, jitter 39 ms, log size 30,000, `RAFT_DEBUG`. `_Static_assert`s catch bad values at build time.
- Each node seeds its jitter with `RAFT_SEED + node ID`: same seed everywhere would give identical timeouts and endless split votes.
- Only `RAFT_DEFAULT_LEADER` and `RAFT_SEED` can be overridden, and must be on all three nodes: `west build-front -p -- -DRAFT_DEFAULT_LEADER=0`, same for rear, and `RAFT_DEFAULT_LEADER=0 sudo -E ./run.sh` on the Pi. Go back with `-p` and no `-D`.
- Every node prints all config values in one line at boot (`N<n> config: ...`): the Pi in `main()`, the STM32s in `errorstate_init()`. Keep the two format strings the same.

## Log entries (team design, decided)

One byte, type in the high nibble and node ID in the low nibble (`0x63` = LINK_A_DOWN for N3); never 0. The table, `ENTRY()` helpers and `entry_valid()` are in `stm32-common/include/catalog.h`. Bus A = the handout's CAN 1.

Storage: two plain arrays, `uint8_t term[LOG_SIZE]` and `uint8_t entry[LOG_SIZE]`; the index is the array position. Implemented once in plain C, `stm32-common/src/raftlog.c` (log + state machine + `get_network()` + print helpers), compiled by the Pi's `build.sh` and both CMakeLists; the caller holds its own lock. Index 1 is the first entry, 0 = none. Before any entry the network is the full topology (all nodes and links up) with the cockpit in error; the leader logs only changes. RAM only: a rebooted node starts empty and is replayed from index 1. 30,000 × 2 B = 60 KB, which fits next to the ~17 KB the apps use now. A struct with a stored `uint16_t` index pads to 6 B per entry (180 KB) and does not fit.

Rules:
1. **No-op requests are ignored.** The leader appends only if the entry changes the state (a link already down stays one entry). Several nodes reporting the same failure, and retries after a redirect, give one entry.
2. **Whose link failed.** A node that hears no one on a bus reports its own link down. A node that hears the others on a bus but not node X reports X's link down.
3. **NODE_DOWN N1 vs COCKPIT_ERROR.** NODE_DOWN N1 = the Pi is unreachable. COCKPIT_ERROR = failure mode, which includes a lost wheel link while the Pi is up. Pi down appends both.
4. **NODE_UP is followed by its links.** `addNode` brings a node back with no links, so the leader also appends LINK_A_UP / LINK_B_UP for each link that works.
5. **CLIENT request numbers.** A client counts up once per new request and repeats the number on a retry; the leader remembers the last number it accepted per node and drops repeats. This is what keeps EVENTs exactly-once.
6. **Reject malformed entries** with `entry_valid()`: types 0 and 0xA–0xF, node IDs outside 1–3, or a node ID on COCKPIT_*.
7. **Log full** (30,000 entries): stop and go to error. `log_append()` refuses; the leader enters error.
8. **Wait after boot before voting.** The log, term and vote are in RAM, so a rebooted node waits one election timeout (hears REF, learns the term) before granting any vote, so it can't vote twice in one term.
9. **Followers at different positions.** APPEND is one broadcast frame, so the leader sends the next entry for the follower that is furthest behind; a follower that already has it just ACKs.
10. **EVENT source:** each test-button press (B1) on an STM32 also sends an EVENT for that node (checkoff 5: N2's EVENT reaches N3 via the Pi).

Console lines, the same format on every node, printed from a thread (never the CAN RX path):
```
[N2] COMMIT idx=12 term=1 LINK_A_DOWN node=3 (0x63)
[N2] NET N1=up(A,B) N2=up(A,B) N3=up(-,B)
[N2] RAFT ...        (Debug only: election and append messages)
```
- BLINK is state only, sent on change and every 100 ms; timing comes from global time.
- With 4 nodes (n = 4), every per-node block still fits (the base IDs 0x018, 0x01C, 0x044, 0x048 are unused) except VOTE_N4 = 0x020, which collides with FAULT.

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
