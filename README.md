# 18-449 Project

Lab 2 firmware for the STM32 Nucleo-F401RE, built with Zephyr.

- `stm32/`: the Zephyr app (empty for now)

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
   git -C zephyr checkout 70be2ff0b56   # pinned commit; everyone uses this one
   west update
   west packages pip --install
   git clone <this-repo-url> 449Project
   ```

## Every new shell

Run from `449Project/`:

```sh
source ../.venv/bin/activate      # Windows: ..\.venv\Scripts\activate.bat
source ../zephyr/zephyr-env.sh    # Windows: ..\zephyr\zephyr-env.cmd
```

## Check that your setup works

Build and flash a stock Zephyr sample. The green LED (LD2) should blink:

```sh
west build -p always -b nucleo_f401re ../zephyr/samples/basic/blinky
west flash
```

## Build, flash, debug our app

Run from `449Project/` once `stm32/` has an app in it:

```sh
west build -p always -b nucleo_f401re stm32   # full rebuild
west flash
west debugserver                              # terminal 1
west debug                                    # terminal 2
```

For debugging, build with optimizations off:

```sh
west build -p always -b nucleo_f401re stm32 -- -DCONFIG_DEBUG_OPTIMIZATIONS=y -DCONFIG_DEBUG_THREAD_INFO=y
```