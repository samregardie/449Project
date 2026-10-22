# 18-449 Project

Firmware for the STM32 Nucleo-F401RE (Zephyr) and the Raspberry Pi cockpit.

- `lab3/`: Lab 3 apps (`stm32-front`, `stm32-rear`, `pi/proxy_receiver`); pins in [`lab3/pins.md`](lab3/pins.md)
- `lab2/stm32/`: Lab 2 Zephyr app
- `setup449`: shell setup script

## Lab 3

### STM32 nodes

In every new shell, from `449Project/`:

```sh
source ./setup449
```

Then, from any folder:

```sh
west build-front     # N2, steering
west flash-front
west build-rear      # N3, drivetrain
west flash-rear
```

Add `-p` for a clean rebuild (`west build-front -p`). With both boards plugged in, add the board's serial number: `west flash-front --dev-id <serial>` (find it with `STM32_Programmer_CLI -l st-link`), or plug in one board at a time.

### Raspberry Pi

```sh
cd lab3/pi/proxy_receiver
sudo ./run.sh
```

It sets up both buses, builds and starts. Enter sends one out-of-range CTRL (bad-input test). Ctrl+C stops it. Watch the buses with `candump -c -t d any`.

## Lab 2

From `449Project/`:

```sh
west build -p always -b nucleo_f401re lab2/stm32   # full rebuild
west flash
west debugserver                                   # terminal 1
west debug                                         # terminal 2
```

For debugging, build with optimizations off:

```sh
west build -p always -b nucleo_f401re lab2/stm32 -- -DCONFIG_DEBUG_OPTIMIZATIONS=y -DCONFIG_DEBUG_THREAD_INFO=y
```

## Zephyr setup (one time)

This repo sits inside a west workspace:

```
zephyrproject/     ← west workspace (not tracked): .venv/ .west/ zephyr/ modules/
└── 449Project/           ← this repo
```

1. Follow the [Zephyr getting-started guide](https://docs.zephyrproject.org/latest/develop/getting_started/index.html) to install the host dependencies and the Zephyr SDK.
2. Install [STM32CubeProgrammer](https://www.st.com/en/development-tools/stm32cubeprog.html) (for `west flash`) and OpenOCD (for `west debug`; on macOS run `brew install openocd`).
3. Create the workspace and clone this repo into it:
   ```sh
   mkdir zephyrproject && cd zephyrproject
   python3 -m venv .venv
   source .venv/bin/activate
   pip install west
   west init -m https://github.com/zephyrproject-rtos/zephyr .
   west update
   west packages pip --install
   git clone <this-repo-url> 449Project
   ```
4. Check it works: the green LED (LD2) should blink.
   ```sh
   source ./setup449
   west build -p always -b nucleo_f401re ../zephyr/samples/basic/blinky
   west flash
   ```

`setup449` must be sourced, not executed. It activates the west venv and the Zephyr environment, and adds the Lab 3 shortcuts.
