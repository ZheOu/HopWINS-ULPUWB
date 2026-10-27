# HopWINS DW1000 integration

`deca_*` comes from Decawave DW1000 Device Driver 04.00.06 in [`Decawave/dwm1001-examples`](https://github.com/Decawave/dwm1001-examples). `hopwins_dw1000.*` supplies this project's STM32 SPI hooks and initialization policy.

The driver owns the DW1000 timing policy: initialization must stay below 3 MHz, runtime must not exceed 20 MHz, and `SYS_STATUS.CPLOCK` must be set before the high-speed switch. The board layer owns the MCU-specific SPI clock, prescaler selection, and register update. `hopwins_dw1000_spi_frequency_hz()` exposes the resulting frequency without exposing those board details to the UWB service.

The original Decawave initialization is retained. It reads XTAL trim, LDO tune, part ID, and lot ID from OTP but never programs OTP. Empty XTAL trim falls back to the driver's mid-range value, empty LDO tune is ignored, and the IDs are not used by this project. `DW1000_INITIALISATION_OPTIONS` remains `DWT_LOADUCODE` because accurate RX timestamps require the LDE microcode.

Antenna delay is not loaded from OTP by this initialization and is not configured by the current project.

The imported `deca_*` files remain unchanged. SPI transfers remain synchronous because the Decawave register API requires completion before returning; UART reporting uses DMA.
