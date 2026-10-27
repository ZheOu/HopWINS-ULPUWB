# HopWINS serial service

The PC link uses USART1 at 4 Mbps. Only transmission is exposed: `hopwins_serial_send()` appends a four-byte big-endian CRC-32/MPEG-2 and places the message in a two-slot TX DMA queue. DMA completion automatically starts the next queued message, so callers do not poll or wait. The configured UART RX resources are not used by this service.

CRC parameters are polynomial `0x04C11DB7`, initial value `0xFFFFFFFF`, no reflection, and no final XOR. Complete words use the STM32 CRC peripheral; the final zero to three bytes are completed in software without padding.

This service does not define message types or UWB packet contents.
