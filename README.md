# 18-449 Project

Lab 2 firmware for the STM32 Nucleo-F401RE, built with Zephyr.

- `lab2/stm32/`: the Zephyr app
- `setup449`: shell setup script (see [Every new shell](#every-new-shell))

This repo sits inside a west workspace:

```
zephyrproject/     ← west workspace (not tracked): .venv/ .west/ zephyr/ modules/
└── 449Project/           ← this repo
```

## One-time setup

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

## Every new shell

Run from `449Project/` (bash or zsh):

```sh
source ./setup449
```

It must be sourced, not executed. It activates the west venv and the Zephyr environment.
## Check that your setup works

Build and flash a stock Zephyr sample. The green LED (LD2) should blink:

```sh
west build -p always -b nucleo_f401re ../zephyr/samples/basic/blinky
west flash
```

## Build, flash, debug our app

Run from `449Project/`:

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
