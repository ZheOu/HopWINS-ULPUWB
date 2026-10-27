# HopWINS UWB service

`hopwins_uwb_service.c` contains the shared DW1000 RF configuration and two task function groups. Call `hopwins_uwb_init()` once, then compose the required start/process functions in the application:

- `hopwins_uwb_periodic_tx_start()` / `hopwins_uwb_periodic_tx_process()`: sends every 5 ms using DW1000 delayed TX. Each UART completion report contains packet type, packet index, actual TX timestamp, and scheduling status.
- `hopwins_uwb_rx_start()` / `hopwins_uwb_rx_process()`: keeps the receiver enabled, polls for good frames or RX errors, and reports the decoded packet fields together with RX timestamps, first-path index and amplitudes, preamble count, CIR power, noise, clock tracking, carrier integrator, frame length, and status.

The two radio tasks are independently callable but must not run concurrently on the same DW1000 without an explicit radio-state coordinator.

Shared RF configuration is channel 5, PRF 64 MHz, 4096-symbol preamble, PAC32, code 9, standard SFD, and 850 kbps. SFD timeout is `4096 + 1 + 8 - 32 = 4073` symbols.

The periodic test payload is exactly three bytes: type `0x11` followed by a big-endian 16-bit packet index. The DW1000 adds the two-byte FCS, so the programmed PHY frame length is five bytes. The 5 ms period is converted using the DW1000 63.8976 GHz device-time counter and equals 319,488,000 device time units, exactly divisible by the delayed-TX 512-DTU resolution.

UART event lines use stable space-separated `key=value` fields. TX completion is reported as `[UWB][TX] event=sent type=0x11 idx=... tx_ts=... status=...`. RX reports begin with `[UWB][RX] valid=... type=... idx=...`; `rx/raw/sys` are timestamps, `fp_q6/pre/amp/cir/noise` are receive-quality values, `ttcki/ttcko/ci/rsd` are clock-tracking values, and `st` is `SYS_STATUS`. Short field names keep the worst-case report below 256 bytes. `status=rescheduled` means the absolute 5 ms slot was missed and the next packet was safely replanned from the current DW1000 time.

The service does not export CIR samples.
