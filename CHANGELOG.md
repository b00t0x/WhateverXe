# Changelog

## 0.6.1

Use the latest [WhateverXe Graphics](https://github.com/b00t0x/WhateverXe-Graphics/releases) package.

### Added

- Add experimental internal display (eDP) and brightness support for Alder Lake-N / Twin Lake and Alder Lake-H/P/U / Raptor Lake-H/P/U GPUs. Not yet tested on hardware.

### Fixed

- Prepare the shared display clock automatically for MIPI DSI with external outputs, avoiding startup and hotplug freezes when the optional host trial is disabled.

- Accept compatible MIPI DSI firmware layouts before BDB 256, inline panel-reset sequences, and the second native panel GPIO bank.
- Publish MIPI DSI timings that exceed base EDID limits through a DisplayID extension, preserving the firmware link timing.
- Allow frame-period-based command completion waits while MIPI DSI video is active, fixing display wake timeouts for panels with a separate display-on sequence.

- Fix system power-off hangs with a USB-C display connected on Alder Lake-class GPUs by releasing idle Type-C PHY ownership during shutdown.

- Fix display freezes during hardware cursor size changes by preparing watermarks for the requested size before the native control update.

- Remove arbitrary MIPI DSI mode-list limits, forward ordinary OS attributes to the native driver, and accept firmware panel orientation metadata without changing physical link timing.

- Fix DEBUG boot freezes and restart loops caused by excessive kernel stack usage.
- Fix multi-display startup and display wake with an experimental MIPI DSI panel alongside HDMI or USB-C outputs on macOS Big Sur.

## 0.6.0

Use the latest [WhateverXe Graphics](https://github.com/b00t0x/WhateverXe-Graphics/releases) package.

### Added

- Add Alder Lake-H/P/U and Raptor Lake-H/P/U GPU support with automatic resource detection and VBT-selected fixed HDMI routes.
- Add experimental fixed TC DisplayPort output with matching AUX wiring; Alder Lake-H has been tested through a DP-to-HDMI adapter.
- Support startup with multiple fixed TC displays, including supported firmware-scaled boot displays.

### Fixed

- Improve display clock ownership, output shutdown and reuse across the shared external-display paths.
- Stop display teardown and reuse when required shutdown steps fail.
- [WhateverXe Graphics 0.3.0](https://github.com/b00t0x/WhateverXe-Graphics/releases) fixes shader compilation failures on Sonoma–Tahoe by expanding supported AIR inputs.

Alder Lake-H hardware checks cover Big Sur–Tahoe. Other newly accepted mobile configurations remain untested. Internal panels and Type-C-managed output on the newly supported mobile GPUs still need implementation; existing DisplayPort reconnection and Sequoia App Store video hangs remain unresolved. See the [README](https://github.com/b00t0x/WhateverXe#tested-features-and-known-issues) for coverage and limits.

## 0.5.0

The newly added GPUs require [WhateverXe Graphics 0.2.0](https://github.com/b00t0x/WhateverXe-Graphics/releases). Its rendering fixes also benefit previously supported GPUs; updating both components is recommended.

### Added

- Add Alder Lake-S graphics acceleration and HDMI/DisplayPort output, including compatible USB-C wiring and up to three displays.
- Accept Tiger Lake-H45, Alder Lake-HX and Raptor Lake-S/HX using shared support. These configurations still need hardware testing.
- Use firmware information to configure more display layouts automatically.

### Fixed

- Avoid some startup panics caused by display hardware not being ready.
- Restore Safari graphics-driver access regardless of where macOS installs Safari updates.
- [WhateverXe Graphics 0.2.0](https://github.com/b00t0x/WhateverXe-Graphics/releases) fixes observed Chrome drawing failures and rendering errors on smaller integrated GPUs.

DisplayPort/USB-C reconnection can still freeze the computer. See the [README](https://github.com/b00t0x/WhateverXe#tested-features-and-known-issues) for tested configurations and other known issues.

## 0.4.0

Add experimental Tiger Lake laptop display support and automatic Display 13 tiling selection.

- Read connector routing from structurally valid firmware VBT tables even when their checksum is stale, while retaining bounds and connector checks.
- Add Tiger Lake internal eDP output with EDID-derived color depth, firmware-derived panel setup and brightness control with SSDT-PNLF.
- Add firmware-selected Tiger Lake USB-C DisplayPort Alt Mode output, including PHY, AUX and lane assignment handling.
- Keep eDP scrambler settings synchronized with the panel during link training to restore normal output after display sleep.
- Remove the `-wex-nopanel` boot argument: it is no longer needed because internal displays are detected automatically on laptops and mini PCs.
- Select Y-tiled scanout automatically on the supported Display 13 backend, removing the OpenCore `AAPL,GfxYTile` requirement.

Laptop display and brightness checks cover Big Sur through Tahoe. HDMI and both tested USB-C ports passed reconnection checks on Tahoe; Tiger Lake and Alder Lake-N mini PCs passed Big Sur/Tahoe regression checks. Automatic Display 13 Y tiling was verified on Alder Lake-N with Big Sur and Tahoe.

This update does not require updating or reinstalling the WhateverXe Graphics package. Fixed DisplayPort reconnection remains a known issue, and system sleep is not validated; see the [README](https://github.com/b00t0x/WhateverXe#tested-features-and-known-issues) for coverage and limitations.

## 0.3.0

Xe-LP graphics compatibility for the configurations described in the [README](https://github.com/b00t0x/WhateverXe#compatibility).

- One WEX binary supports Big Sur through Tahoe on the implemented Tiger Lake and Alder Lake-N / Twin Lake backends.
- Graphics installation uses the signed [WhateverXe Graphics pkg](https://github.com/b00t0x/WhateverXe-Graphics/releases), with no Xe System-volume patch or OCLP dependency.
- Includes cross-version OS/driver ABI adaptations, OpenCL compatibility and late semaphore mapping invalidation fixes.
- Supports signed Data graphics bundles with Mod AMFIPass, without `amfi=0x80` or `ipc_control_port_options=0`.
- Preserve debug information in the bundled dSYM before stripping the kext.
- Tiger Lake and Alder Lake-N have completed the six-OS functional checks described in the README. Known issues and hardware coverage limits still apply.

## 0.2.3

Fix DisplayPort startup and hotplug issues.

- Fix a kernel panic when booting with only DisplayPort connected.
- Fix DisplayPort detection when first connected after an HDMI-only boot.

The text-rendering fix is provided separately by the Xe root patch in [OpenCore Legacy Patcher 3.0.0-rc.3+m.2](https://github.com/b00t0x/OpenCore-Legacy-Patcher/releases/tag/3.0.0-rc.3%2Bm.2).

## 0.2.2

Xe-LP graphics compatibility for the configurations described in the [README](https://github.com/b00t0x/WhateverXe#compatibility).
