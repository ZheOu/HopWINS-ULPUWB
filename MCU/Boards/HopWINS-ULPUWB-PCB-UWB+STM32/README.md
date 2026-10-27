# HopWINS ULPUWB UWB+STM32 board

DW1000 connections currently configured:

| Signal | STM32F411 |
|---|---|
| CS | PB12, GPIO output |
| SCK | PB13, SPI2 |
| MISO | PB14, SPI2 |
| MOSI | PB15, SPI2 |

SPI2 is clocked from the 40 MHz APB1 peripheral clock. The board layer provides
two hardware speed settings: low uses prescaler 16 (2.5 MHz), and high uses
prescaler 2 (20 MHz). The DW1000 driver decides when to use each setting and
checks the device limits before asking the board to switch.

PC serial connections:

| Signal | STM32F411 |
|---|---|
| PC TX / MCU RX | PA10, USART1_RX |
| PC RX / MCU TX | PA9, USART1_TX |
