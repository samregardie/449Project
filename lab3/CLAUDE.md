# lab3/CLAUDE.md

Lab 3: Dual-Redundant CAN and Real-Time Analysis (due 2026-10-22, 11:59pm). The spec is `../docs/handouts/Lab3 Handout.pdf`; re-read the relevant part before answering anything specific. The repo-wide rules in `../AGENTS.md` still apply (west workspace is upstream, no new scaffolding, AI-use policy).

## Current state of this folder

```
lab3/
  pi/proxy_receiver/   N1 cockpit (C, pigpio, SocketCAN on canA/canB). Payload layouts are in the receiver.c header comment.
  stm32-common/        Not an app. .c/.h shared by both STM32 nodes; each node's CMakeLists.txt adds them.
  stm32-front/         N2 steering app (NODE_ID=2): servo
  stm32-rear/          N3 drivetrain app (NODE_ID=3): drive (PI loop), motor, encoder
  stm32-can-test/      Bring-up test, same build on both boards: counter on A and B, prints RX. Uses the front overlay
  Schematic/           Lab 2 KiCad project, base for the dual-bus schematic
```
Both apps build. `stm32-common/` has `canbus` (dedup, send on both), `ctrl` (CTRL CRC and range check) and `errorstate` (HB_TX, HB_MON, DEG/ERR, error state). `blinker` follows BLINK; the test button is B1 (PC13), handled in `errorstate`. Not done yet: FORCE_TX, watchdog, RELAY, CAN_RX/TX test-point pins.
- Common code must not reference node-only hardware or modules. Both overlays need the same node labels for shared hardware (CAN, DEG_A/B, ERR, blinkers). Node differences go through `NODE_ID` or init arguments from each `main.c`.
- Both `app.overlay` files use the same CAN wiring, which differs from the handout's suggested wiring: SPI1 on D3 (SCK), D5 (MISO) and D4 (MOSI); CS A = D10, B = D9; INT A = D6, B = D7. The rear's Lab 2 pins that collided with it were moved (see its header comment).

## System

```
Logitech wheel --USB--> laptop proxy --UDP:8000 (20 ms)--> Pi (N1, cockpit)
                                                            │ canA = bus A, canB = bus B
                       CAN A ════════╪═══════════╪═══════════╪════  500 kbit/s, 120 Ω each end
                       CAN B ════════╪═══════════╪═══════════╪════  500 kbit/s, 120 Ω each end
                                N2 steering   N3 drivetrain
                                (STM32 #1)    (STM32 #2)
```
- Six XIAO CAN modules (MCP2515 on SPI + SN65HVD230), two per node, one per bus. 3.3 V only, never 5 V.
- Lab 2 sensors and actuators are optional. Checkoff is scored on LEDs and GPIO test points: 4 blinkers, DEG_A/DEG_B per node, motor PWM/DIR pins scoped instead of driving the H-bridge.
- Force feedback (0x300) payload is a dummy counter.

## Wiring (handout suggestions; must match overlay and config.txt)

| Module pin | Nucleo (SPI1)                 | Pi (SPI0)                        |
|------------|-------------------------------|----------------------------------|
| D8 SCK     | D13 / PA5 (shared)            | GPIO11 pin 23 (shared)           |
| D9 MOSI    | D11 / PA7 (shared)            | GPIO10 pin 19 (shared)           |
| D10 MISO   | D12 / PA6 (shared)            | GPIO9 pin 21 (shared)            |
| D7 CS      | A: D10/PB6, B: D9/PC7         | A: CE0 pin 24, B: CE1 pin 26     |
| D6 INT     | A: D2/PA10, B: D4/PB5 (EXTI)  | A: GPIO25 pin 22, B: GPIO24 pin 18 |

- Check the crystal marking on the module: `osc-freq` (Zephyr) and `oscillator=` (Pi) must match it, or the node runs at half/double bit rate.
- MCP2515 SPI max is 10 MHz.
- PA5 is also the user LED LD2.
- Termination: exactly one 120 Ω at each end of each bus (P1 pad or a discrete resistor, not both). Powered off, CANH–CANL should measure 60 Ω.
- Common ground across Pi, both Nucleos and the bus breadboard before anything else.
- Pulling a node's jumper from the bus rail to its module's screw terminal is the fault injection.

## Message catalog (template, fixed by the handout)

All standard 11-bit IDs, DLC 8, sent on **both** buses. Byte 0 = 4-bit seq counter (low nibble) + 4 flag bits. Receivers drop the second copy by `(ID, seq)`.

| ID    | Name  | From | To     | Period / trigger                       | Deadline |
|-------|-------|------|--------|----------------------------------------|----------|
| 0x010 | CTRL  | N1   | N2, N3 | every UDP update (20 ms)               | 2 ms     |
| 0x020 | FAULT | any  | all    | zone fail / out-of-range, min 20 ms    | 100 ms   |
| 0x100 | HB_N1 | N1   | N2, N3 | 20 ms                                  | 20 ms    |
| 0x101 | HB_N2 | N2   | N1, N3 | 20 ms                                  | 20 ms    |
| 0x102 | HB_N3 | N3   | N1, N2 | 20 ms                                  | 20 ms    |
| 0x200 | BLINK | N1   | N2, N3 | state change + 500 ms toggle, min 20 ms | 100 ms start; N2/N3 edges within 1 ms |
| 0x300 | FORCE | N2   | N1     | 50 ms                                  | 50 ms    |

Payloads: CTRL = steer int16, throttle u8, brake u8, buttons u8, CRC-8. FAULT = zone id, reason. HB = zone state, bus A/B health bits. BLINK = state (off/L/R/hazard), phase bit. FORCE = int8.

The cockpit is the blinker master. N2 and N3 toggle on receiving BLINK, never on their own timers.

## Task set (template; priorities are the team's)

- **N1 (Pi):** UDP_RX, CTRL_TX, BLINK_TX, HB_TX, HB_MON/FORCE_RX. No WCET; Pi latency is measured (max and p99, with and without `stress`).
- **N2 (front):** CAN_RX_A/B (ISR + MCP2515 driver thread + dispatch), STEER, BLINK_F, FORCE_TX, HB_TX, HB_MON, TEST_BTN, plus RELAY (Part 4).
- **N3 (rear):** CAN_RX_A/B, DRIVE, PID (10 ms), BLINK_R, HB_TX, HB_MON, TEST_BTN.

The handout table has the placeholder WCETs. Each WCET used in the report must say whether it was measured or assumed, and how.

## What the code must show (Part 5)

- Two CAN devices from devicetree. `can_add_rx_filter` registers the catalog IDs on both. One dispatch path dedups by `(ID, seq)` before any task sees a frame.
- A send helper that queues on both controllers back to back and bumps seq once.
- HB_MON tracks last-seen time per zone per bus. Thresholds live in one place.
  - 3 missed (60 ms) on one bus: that bus's DEG LED on.
  - 3 missed on both buses: zone failed, error state.
- RELAY on N2 as its own thread with an explicit priority.
- Error state machine:
  - Powers up in error.
  - Exits when heartbeats from both other zones are seen on at least one bus and a valid CTRL has arrived.
  - Enters on test button, out-of-range CTRL (also sends FAULT), or 3 missed HBs on both buses.
- Independent watchdog enabled on each STM32.
- Bus-off recovery: back within 100 ms of the jumper going back in (`can_recover` or auto-recovery).
- No SPI, logging or allocation in ISRs. Shared state protected by Zephyr primitives, not `volatile`.
- A way to inject a bad CTRL (a Pi flag or `cansend`).

## Test-point boards (one breadboard per node, this row order)

| Row | N1 (Pi)  | N2 (front) | N3 (rear) |
|-----|----------|------------|-----------|
| 1-2 | GND      | GND        | GND       |
| 3   | UDP_RX   | CAN_RX_A   | CAN_RX_A  |
| 4   | CMD_TX   | CAN_RX_B   | CAN_RX_B  |
| 5   | CAN_TX_A | CAN_TX_A   | CAN_TX_A  |
| 6   | CAN_TX_B | CAN_TX_B   | CAN_TX_B  |
| 7   | DEG_A    | DEG_A      | DEG_A     |
| 8   | DEG_B    | DEG_B      | DEG_B     |
| 9   | -        | SRV        | PWM_SET   |
| 10  | -        | FL         | PWM_OUT   |
| 11  | -        | FR         | DIR_A     |
| 12  | -        | RELAY_TX   | RL        |
| 13  | -        | ERR        | RR        |

Definitions:
- CAN_TX_x toggles on the call that queues on that controller.
- CAN_RX_x toggles in dispatch, after the SPI read.
- ERR is high while the node is in the error state.

## Analysis (Parts 3–4), for reviewing the team's numbers

- **Node RTA:** `R_i = C_i + B_i + Σ_{hp(i)} ceil(R_i/T_j) C_j`. ISRs count as the highest priority. The shared SPI bus is a blocking source.
- **CAN RTA (Davis et al. 2007):** `w = B + Σ_{hp} ceil((w + J_j + τ)/T_j) C_j`, then `R = J + w + C`.
  - τ = 2 µs. A DLC-8 frame is C = 270 µs, and B = 270 µs.
  - J for each frame is the sender's response time, or the measured Pi latency for N1.
- **Utilization:** single-bus baseline is about 8.6%. The ceiling is 50% (R4.6). The relay adds forwarded copies on bus B.
- **Chains:** brake/throttle 2 ms, steering 50 ms, blinker start 100 ms, blinker sync ≤ 1 ms, error propagation 100 ms.
- **Relay case (scenario c, N1 off B and N3 off A):** the brake chain adds CAN_RX on N2, RELAY, and a second bus hop. With placeholder numbers that is Pi + ~1.83 ms, so something has to change. The handout accepts any change the analysis backs up.

## Checkoff sequence (abridged)

1. Cold start in error.
2. Every catalog ID shows on both buses (`candump -c any`).
3. Steering and blinkers work; blinker edges within 1 ms over 10 edges.
4. Brake chain scope capture.
5. Single-jumper faults: DEG on within 60 ms, off within 100 ms.
6. Whole bus A lost.
7. Relay hop (scenario c).
8. Both jumpers of one STM32 pulled: fail-safe and system error within 100 ms; recovers within 500 ms with no reset.
9. Test button: single press enters error, double press exits.
10. Bad CTRL puts the receiver in error and sends FAULT.
11. Analysis walk-through.

The team must explain every design decision and number, so explain and review rather than hand over finished controllers, relay logic or analysis tables (see the AI-use policy in `../AGENTS.md`).

## Commands

From the repo root, after `source ./setup449`:
```sh
west build -p always -b nucleo_f401re -d build-front lab3/stm32-front   # N2
west build -p always -b nucleo_f401re -d build-rear  lab3/stm32-rear    # N3
west flash -d build-front                                                # flash one board at a time
west build -p always -b nucleo_f401re ../zephyr/samples/drivers/can/counter -- -DDTC_OVERLAY_FILE=<overlay>   # Part 0 bring-up
```
Pi bring-up: add `dtparam=spi=on` and the two `mcp2515-can0`/`can1` overlays to `/boot/firmware/config.txt`.

The kernel names CAN interfaces in probe order, not by chip select: on our Pi, `can0` came up as the CE1 module (bus B). A udev rule names them by chip select instead. It lives only on the Pi's SD card, so redo it after a re-flash:
```sh
sudo tee /etc/udev/rules.d/70-can-names.rules <<'EOF'
SUBSYSTEM=="net", ACTION=="add", KERNELS=="spi0.0", NAME="canA"
SUBSYSTEM=="net", ACTION=="add", KERNELS=="spi0.1", NAME="canB"
EOF
sudo reboot
```
If `ip link` shows `can0`/`can1` instead, the rule didn't match. Then:
```sh
sudo ip link set canA up type can bitrate 500000   # same for canB (pi/proxy_receiver/run.sh does this plus build and start)
ip -details link show canA                          # expect ERROR-ACTIVE
candump -c -t d any
```
Useful upstream references (read-only):
- `../zephyr/dts/bindings/can/microchip,mcp2515.yaml`
- `../zephyr/drivers/can/can_mcp2515.c`
- `../zephyr/samples/drivers/can/counter`
