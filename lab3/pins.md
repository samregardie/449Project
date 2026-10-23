# Lab 3 pin assignments

From `stm32-front/app.overlay`, `stm32-rear/app.overlay` and `pi/proxy_receiver/receiver.c`.
"Old" is the Lab 2 pins as they were in the `lab3-setup` commit, before CAN.
STM32 pins are written Arduino name / MCU pin. CN7 and CN10 are the morpho headers.
Update this file whenever an overlay changes.

## CAN modules (same wiring on both STM32 boards)

| Module pin | Signal | Nucleo pin | Notes |
|---|---|---|---|
| D8 | SCK (shared) | D3 / PB3 | SPI1, moved off the handout's D13 |
| D10 | MISO (shared) | D5 / PB4 | |
| D9 | MOSI (shared) | D4 / PB5 | |
| D7 | CS, bus A | D10 / PB6 | |
| D7 | CS, bus B | D9 / PC7 | |
| D6 | INT, bus A | D6 / PB10 | EXTI 10 |
| D6 | INT, bus B | D7 / PA8 | EXTI 8 |

Modules are 3.3 V only.

## Front, N2

| Signal | Old | New |
|---|---|---|
| Servo PWM (SRV) | A0 / PA0 (TIM5 CH1) | A0 / PA0 |
| FL blinker | D14 / PB9 | D12 / PA6 |
| FR blinker | D12 / PA6 | D14 / PB9 |
| DEG_A | - | A1 / PA1 |
| DEG_B | - | A2 / PA4 |
| ERR | - | A3 / PB0 |
| CAN_RX_A | - | D2 / PA10 |
| CAN_RX_B | - | D8 / PA9 |
| CAN_TX_A | - | D11 / PA7 |
| CAN_TX_B | - | D15 / PB8 |
| RELAY_TX | - | A4 / PC1 |
| Test button | - | B1 / PC13 (blue button) |
| CAN modules | - (SPI1 disabled) | see above |

Free: A5 / PC0. D13 / PA5 is free but drives LD2.

EXTI lines in use: CAN 10, 8; button 13.

## Rear, N3

| Signal | Old | New | Why it moved |
|---|---|---|---|
| Right motor PWM (IN1) | D2 / PA10 (TIM1 CH3) | D2 / PA10 | |
| Left motor PWM (IN3) | D4 / PB5 (TIM3 CH2) | D11 / PA7 (TIM3 CH2) | D4 is SPI MOSI |
| Right motor DIR (IN2) | D3 / PB3 | A3 / PB0 | D3 is SPI SCK |
| Left motor DIR (IN4) | D5 / PB4 | A5 / PC0 | D5 is SPI MISO |
| RL blinker | D11 / PA7 | D12 / PA6 | D11 is left motor PWM |
| RR blinker | D10 / PB6 | D14 / PB9 | D10 is CAN A CS |
| Right encoder A | D6 / PB10 | A1 / PA1 | D6 is CAN A INT |
| Right encoder B | D7 / PA8 | A2 / PA4 | D7 is CAN B INT |
| Left encoder A | D8 / PA9 | D8 / PA9 | |
| Left encoder B | D9 / PC7 | A0 / PA0 | D9 is CAN B CS |
| DIR_A | D15 / PB8 | D15 / PB8 | |
| PWM_SET | A4 / PC1 | A4 / PC1 | |
| PWM_OUT | right motor PWM, D2 | same | |
| DEG_A | - | PC10 (CN7 pin 1) | Arduino header full |
| DEG_B | - | PC11 (CN7 pin 2) | |
| ERR | - | PC12 (CN7 pin 3) | |
| RELAY_TX | - | PC2 (CN7 pin 35) | Not in the handout's table: N3 also relays (N1/N2 split) |
| CAN_RX_A | - | PC9 (CN10 pin 1) | |
| CAN_RX_B | - | PC8 (CN10 pin 2) | |
| CAN_TX_A | - | PC6 (CN10 pin 4) | |
| CAN_TX_B | - | PC5 (CN10 pin 6) | |
| Test button | - | B1 / PC13 | |
| CAN modules | - (SPI1 disabled) | see above | |

Free: only D13 / PA5 (LD2) on the Arduino header; use morpho pins for more.

EXTI lines in use: CAN 10, 8; encoders 9, 0, 1, 4; button 13. Every interrupt pin needs a different pin number.

## Pi, N1 (BCM numbers, header pin in brackets)

| Signal | Old (Lab 2) | New |
|---|---|---|
| UDP_RX | GPIO2 (3) | GPIO2 (3) |
| CMD_TX | GPIO3 (5) | GPIO3 (5) |
| CAN_TX_A | - | GPIO5 (29) |
| CAN_TX_B | - | GPIO6 (31) |
| DEG_A LED | - | GPIO13 (33) |
| DEG_B LED | - | GPIO19 (35) |
| ERR | - | GPIO26 (37) |
| UART to STM32 | serial0 / ttyAMA5 | removed (CAN instead) |
| SPI SCK / MOSI / MISO | - | GPIO11 (23) / GPIO10 (19) / GPIO9 (21) |
| CS bus A / B | - | CE0 = GPIO8 (24) / CE1 = GPIO7 (26) |
| INT bus A / B | - | GPIO25 (22) / GPIO24 (18) |

The CAN pins must match `/boot/firmware/config.txt` (`interrupt=`). The udev rule names CE0 canA and CE1 canB.

## Test-point boards (handout row order)

| Row | Pi | N2 front | N3 rear |
|---|---|---|---|
| 1-2 | GND | GND | GND |
| 3 | UDP_RX GPIO2 | CAN_RX_A D2 | CAN_RX_A PC9 |
| 4 | CMD_TX GPIO3 | CAN_RX_B D8 | CAN_RX_B PC8 |
| 5 | CAN_TX_A GPIO5 | CAN_TX_A D11 | CAN_TX_A PC6 |
| 6 | CAN_TX_B GPIO6 | CAN_TX_B D15 | CAN_TX_B PC5 |
| 7 | DEG_A GPIO13 | DEG_A A1 | DEG_A PC10 |
| 8 | DEG_B GPIO19 | DEG_B A2 | DEG_B PC11 |
| 9 | - | SRV A0 | PWM_SET A4 |
| 10 | - | FL D12 | PWM_OUT D2 |
| 11 | - | FR D14 | DIR_A D15 |
| 12 | - | RELAY_TX A4 | RL D12 |
| 13 | - | ERR A3 | RR D14 |

The Pi's ERR (GPIO26) and N3's ERR (PC12) aren't in the handout's table, but wire them to LEDs: checkoff step 9 expects ERR high on all three.
