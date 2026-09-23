# AGENTS.md

Guidance for AI coding agents (Claude Code, Codex, etc.) in this repo.

## What this is

Team repo for an embedded-systems course, Lab 2: Sensor/Actuator Bring-up (due 2026-10-01). Target: STM32 Nucleo-F401RE running Zephyr. The spec is `Lab2_handout.pdf`; flashing/debugging notes are `Flashing_and_Debugging_Your_Nucleo.pdf`.

The repo is kept minimal on purpose. `stm32/` is where the Zephyr app goes (currently empty). Don't add folders, tooling or scaffolding the team didn't ask for.

The repo sits inside a west workspace at `../` (`.venv/`, `.west/`, `zephyr/`, `modules/`). That is upstream code: never edit it, and don't `git pull` in `../zephyr/` (pinned to commit `70be2ff0b56`). Put hardware customization in an app overlay (`stm32/boards/nucleo_f401re.overlay`), not in `../zephyr/boards/`.

## Commands

Every new shell, from the repo root:
```sh
source ../.venv/bin/activate
source ../zephyr/zephyr-env.sh
```

```sh
west build -p always -b nucleo_f401re ../zephyr/samples/basic/blinky   # sanity-check toolchain + board
west build -p always -b nucleo_f401re stm32                            # build our app
west build                                                             # incremental rebuild
west flash                                                             # uses STM32CubeProgrammer
west debugserver   # terminal 1 (OpenOCD)
west debug         # terminal 2
```
Debug build: append `-- -DCONFIG_DEBUG_OPTIMIZATIONS=y -DCONFIG_DEBUG_THREAD_INFO=y`. If GDB keeps stopping in `z_arm_reset()`, set a breakpoint and let the program hit it. Never flash J-Link firmware onto the ST-Link.

## Lab 2 requirements (summary of the handout)

```
Logitech wheel --USB--> laptop --UDP:8000--> Raspberry Pi --UART--> STM32
                                                                     ├─ 2 DC motors + encoders (L298N)
                                                                     ├─ steering servo
                                                                     ├─ 4 blinkers (FL, FR, RL, RR)
                                                                     └─ 3 current sensors
```
- **Link (team designs it):** command every UDP update and at least every 50 ms. STM32 sends a status frame every 20 ms ±10% (currents + state). 3 missed commands → fail-safe. Reject malformed/out-of-range frames. Framing must resync mid-stream.
- **Motors:** throttle → target velocity, closed-loop PID on the encoder average. Brake = PWM off + dynamic braking; brake beats throttle.
- **Servo:** follows the wheel; clamp so it never stalls at the stops.
- **Blinkers:** 1 Hz, 50% duty, self-cancel after a turn. Error state = all 4 at 2 Hz.
- **Error state:** at power-up, on self-test button (double press exits), link loss, or out-of-range command. Outputs go safe.
- **Timing:** throttle/brake 2 ms, steering 50 ms, blinkers 100 ms, self-test 10 ms, link-loss fail-safe 100 ms.
- **Test-point GPIOs:** CMD_RX, PWM_SET toggles; PWM_OUT, DIR_A, SRV, FL/FR/RL/RR scoped directly.
- No blocking, logging or allocation in ISRs. Share state with Zephyr sync primitives, not `volatile`.

Hardware safety: check 5 V tolerance per pin (analog pins usually aren't), the Pi is 3.3 V only, never drive motors/servo from a GPIO, power the servo from the regulator, common ground everywhere.

## AI-use policy

The course allows AI to explain and review, not to do the design. The team must be able to explain every decision at checkoff. Prefer explaining and reviewing over writing complete controllers or wiring answers, unless explicitly asked for code.
