# Device selection and DeviceProperties

## Automatic selection

WEX selects the GPU from its physical PCI ID and detects its actual resources automatically. CPU-model, machine-name and resource-count settings are unnecessary.

| Family | IDs with an implemented backend |
| --- | --- |
| Tiger Lake-U / H35 / H45 | `9A40`, `9A49`, `9A59`, `9A60`, `9A68`, `9A70`, `9A78`, `9AC0`, `9AC9`, `9AD9`, `9AF8` |
| Alder Lake-N / Twin Lake | `46D0`–`46D4` |
| Alder Lake-H / P / U | `4626`, `4628`, `462A`, `46A0`–`46A3`, `46A6`, `46A8`, `46AA`, `46B0`–`46B3`, `46C0`–`46C3` |
| Raptor Lake-H / P | `A720`, `A7A0`, `A7A8`, `A7AA`, `A7AB` |
| Raptor Lake-U | `A721`, `A7A1`, `A7A9`, `A7AC`, `A7AD` |
| Alder Lake-S / HX | `4680`, `4682`, `4688`, `468A`, `468B`, `4690`, `4692`, `4693` |
| Raptor Lake-S / HX | `A780`, `A781`, `A782`, `A783`, `A788`, `A789`, `A78A`, `A78B` |

An implemented backend does not imply hardware validation for every ID or connector. See [compatibility and tested configurations](../README.md#compatibility).
Raptor Lake-U internal DSI and acceleration have been verified on MiniBook X U300 (`A7A9`) with Big Sur and Tahoe. Rocket Lake remains unsupported.

Memory configuration can change the PCI ID, such as `46A6` to `4626` in a single-channel Alder Lake-H configuration. No override is needed; use updated WEX and Graphics components together.

## Advanced override: device-id

An override is normally unnecessary. To select an existing backend explicitly, set `device-id` on the iGPU under OpenCore's `DeviceProperties > Add`.
Use **Data containing four little-endian bytes**, with the 16-bit ID followed by two zero bytes. For example, `D2460000` selects `46D2`.

An override cannot make unsupported hardware compatible or change its physical resources. Some backends require the original physical ID. Invalid or unsupported values are rejected; remove obsolete ICL or other GPU spoofs before using automatic selection. Restart after changing the selection.

## Display name

A user-supplied `model` property is preserved. Otherwise, WEX uses the physical GPU's name. Names do not control compatibility or resource detection, and Metal may still show the Apple driver's internal name. Do not use a device-ID override merely to change the displayed name.
