# WhateverXe

[日本語](README_ja.md)

WhateverXe is a Lilu plugin that adapts Apple graphics drivers for selected
Intel Xe-LP integrated GPUs. It is derived from WhateverGreen.

This project is experimental and requires the signed graphics package from
[WhateverXe Graphics](https://github.com/b00t0x/WhateverXe-Graphics). It installs
the graphics stack under `/Library` and updates the Auxiliary Kernel Collection
(AuxKC), without patching System files or snapshots. WEX and the graphics package
have independent release versions. Use the latest releases of both WEX and
WhateverXe Graphics.

## Compatibility

One WEX binary and one graphics pkg cover the following macOS versions:

| macOS | Required version |
| --- | --- |
| Big Sur | 11.7.11 |
| Monterey | 12.7.6 |
| Ventura | 13.7.8 |
| Sonoma | 14.8.9 |
| Sequoia | 15.8.x |
| Tahoe | 26.7 or newer |

Big Sur through Sonoma require the qualified kernel identities. Sequoia and
Tahoe accept servicing updates within their OS family while retaining runtime
instruction and ABI checks. Future updates are not prevalidated.

WEX selects the OS ABI and GPU backend independently. OS-specific graphics
payloads and the common AppleIntelTGLCompat provider are installed by WhateverXe
Graphics; GPU generation and connector routing are handled by WEX.

### Tested on hardware

| GPU family | Tested configurations |
| --- | --- |
| Tiger Lake-U | 48- and 80-EU graphics; desktop-style and laptop display layouts |
| Alder Lake-N | 16- and 24-EU graphics; two HDMI displays |
| Alder Lake-H | 96-EU graphics (`46A6`); fixed TC HDMI and DisplayPort through a DP-to-HDMI adapter |
| Alder Lake-S | UHD Graphics 770 (`4690`); HDMI, DisplayPort and a USB-C port wired as fixed DisplayPort |

These are tested configurations within each family, not a guarantee for every
model or connector. See [tested features and known issues](#tested-features-and-known-issues)
for OS coverage and remaining problems.

### Expected to work, not yet tested on hardware

- Other supported Tiger Lake-U/H35 configurations, including 96-EU graphics.
- Tiger Lake-H45 (GT1): `9A60`, `9A68`, `9A70`.
- Other supported Alder Lake-N configurations and Twin Lake.
- Other Alder Lake-H/P/U and Raptor Lake-H/P/U configurations with supported fixed HDMI or fixed TC DisplayPort wiring.
- Experimental internal eDP on Alder Lake-N / Twin Lake and Alder Lake-H/P/U / Raptor Lake-H/P/U: a single firmware-described Combo A/B panel with PPS0 and PCH PWM brightness. This path has local CPU and donor replay coverage only; use the corresponding WhateverXe Graphics release with this plugin. See [display limits](docs/device-selection.md#display-limits).
- Other supported Alder Lake-S configurations, Alder Lake-HX, Raptor Lake-S and Raptor Lake-HX.

These devices use implemented support shared with the tested families. Their
IDs are accepted automatically; no CPU-model or machine-specific setting is
needed. Hardware validation is still outstanding. All accepted IDs and resource
limits are listed in [device selection](docs/device-selection.md).

### Further implementation required

- On Alder Lake-N / Twin Lake and Alder Lake-H/P/U / Raptor Lake-H/P/U,
  external Combo-PHY DisplayPort and Type-C-managed output need
  further implementation. eDP panels using other power/backlight methods also need additional support. Accepting the GPU ID does not enable these outputs.
- Rocket Lake-S: its display power and clock handling differ from Tiger Lake.
  It is not supported by this release, and no hardware validation is scheduled.

### No support planned

- Xe-LPG, including Meteor Lake integrated graphics.
- Xe2 and later graphics architectures.

## Required components

Download the **RELEASE** ZIP from [Releases](https://github.com/b00t0x/WhateverXe/releases).
The DEBUG ZIP is for troubleshooting. Each ZIP contains only `WhateverXe.kext`
and its matching dSYM. `SHA256SUMS` contains checksums for both archives.
See [CHANGELOG](CHANGELOG.md) for release notes.

| Component | Role |
| --- | --- |
| Lilu | Runtime required by WhateverXe |
| WhateverXe.kext | Load through OpenCore after Lilu |
| [WhateverXe Graphics pkg](https://github.com/b00t0x/WhateverXe-Graphics/releases) | Install the signed graphics stack into `/Library` and rebuild AuxKC |
| [AMFIPass v1.4.1 Mod](https://github.com/b00t0x/WhateverXe-Graphics/blob/main/extras/AMFIPass-v1.4.1-Mod-RELEASE.zip) | Load through OpenCore for the modified graphics stack |

Configure SIP with `csr-active-config=03080000` (value `0x803`). Full SIP is not
supported. With the signed graphics package and linked Mod AMFIPass,
`amfi=0x80` and `ipc_control_port_options=0` are not required.

Tahoe requires `CoreMedia / AllowOnePassMetalScaling` to be disabled; the
WhateverXe Graphics installer applies this setting.

Boot the target OS with the latest WEX before installing the latest graphics
pkg. If an older Xe System patch is installed, restore it using its original
recovery procedure first. The pkg does not change EFI, System files, snapshots
or SIP settings. See [installation and removal](https://github.com/b00t0x/WhateverXe-Graphics#install)
for the package instructions and the standalone uninstall script.

Extract `AMFIPass.kext` from the linked **Mod** archive, place it in
`EFI/OC/Kexts`, and enable it in `Kernel > Add`. Replace an existing AMFIPass
with this Mod build; do not load both copies. Upstream AMFIPass does not support
the Mod signing chain.

If you use OCLP for other root patches, use the
[custom OCLP build](https://github.com/b00t0x/OpenCore-Legacy-Patcher), which uses
the same Mod signing chain. Xe graphics installation itself uses WhateverXe
Graphics independently of OCLP.

WhateverGreen and WhateverXe must not be enabled together, including in
mixed-GPU configurations.

## Configuration

GPU selection is automatic from the PCI device ID. Connector routing is read
from firmware VBT where supported; fixed HDMI and fixed DisplayPort layouts
normally need no manual wiring properties. Advanced `device-id` overrides are
described in [device selection](docs/device-selection.md).

On supported GPUs, WhateverXe also normalizes the detected Intel Management
Engine device name to `IMEI`, as WhateverGreen does. A separate ACPI rename
from `HECI` or `MEI` to `IMEI` is unnecessary.

Tiger Lake automatically selects VBT-described fixed DisplayPort routes on TC1–TC4 when AUX wiring matches the native controller. Fixed DP and DP Alt Mode use the detected FIA layout. TC3/TC4 fixed DP and non-modular FIA support are implemented but have not been verified on hardware. TC5/TC6 and cross-routed AUX require further backend support.

Alder Lake-N / Twin Lake and Alder Lake-H/P/U / Raptor Lake-H/P/U use
VBT-described Combo A/B and fixed TC1–TC4 HDMI routes. Experimental fixed
TC1–TC4 SST DisplayPort requires matching VBT AUX wiring. The explicit
Alder Lake-N HDMI property applies only to Combo routes.

ADL-H has been tested with fixed HDMI outputs and a DP-to-HDMI adapter,
including three connected displays at startup, firmware-scaled boot displays
and DP reconnection. Direct DP sinks and other GPUs in these families have
not been verified with the fixed TC DP path. Experimental USB-C DP Alt Mode
supports VBT-described external TC1–TC4 DP wiring with matching AUX. It reuses
SST link training with Type-C ownership and the detected physical lane assignment.
MST, Thunderbolt, cross-routed AUX and USB-C boot-display adoption remain unsupported.
Alder Lake-N USB-C output has been tested at 1080p60 through an HDMI adapter on Big Sur, including Metal and OpenGL acceleration. USB-C hotplug, sleep/wake and other adapter types have not been verified.

ADL-P/RPL-P uses its physical ID and detected GPU resources automatically.
No device-ID override or machine-specific AGDP kext is required.
`-wex-noaccel` retains framebuffer-only operation.

The experimental `-wex-dsi-hdmi-test` argument transfers a qualified Display13 DSI-A GOP to Combo B at 1080p60 for development. It stops the internal display and retires the temporary HDMI resources when policy switches to another connected output. It is disabled by default and does not provide DSI support or general cold startup.

The optional `-wex-dsi-audit` argument checks the supported MIPI DSI firmware layout and pre-native GOP state, including peer engines and the stolen-memory GGTT prefix, then publishes diagnostic summaries. It is disabled by default; the audit does not program the display or enable DSI output.

The optional `-wex-dsi-adopt` argument selects experimental MIPI DSI firmware-GOP adoption on a supported panel profile. It preserves the firmware link and exposes a synthetic EDID derived from the panel profile for native path and fixed-mode accelerated desktop output. OS-requested 90°/270° rotation and scaled GPU source modes retain the physical panel timing; orientation and HiDPI mode selection are configured in macOS. OS-provided scaled modes are not restricted to a preferred resolution or a fixed list capacity; available HiDPI sizes depend on the macOS display configuration. Native PWM brightness requests and readback use the existing AppleBacklight interface on supported DSI profiles; brightness zero sets zero PWM duty. With display off/on enabled below, requests made while the panel is off are saved and applied before its backlight is enabled on wake. On macOS Big Sur, the internal HiDPI panel has been tested with up to two external HDMI/USB-C displays, including accelerated rendering and display off/on. System sleep and full lifecycle validation remain under development. Do not combine it with `-wex-dsi-hdmi-test`. The additional `-wex-dsi-panel-only` argument temporarily excludes external outputs from online detection and mode setting during internal-panel development; physical output wiring is unchanged. Omit it to test external coexistence. The optional `-wex-dsi-command-test` runs a single DCS NOP/drain trial during DSI adoption; it does not enable panel power cycling. The optional `-wex-dsi-panel-test` tests one panel power/reset/initialization cycle before native startup while retaining the host clock and PHY; complete host reinitialization remains under development. The additional `-wex-dsi-host-test` includes the panel cycle and restarts the exclusively retained PLL, PHY and DSI host from the panel profile. These bootstrap experiments do not enable runtime display/system sleep. The optional `-wex-dsi-stop-test` additionally permits a one-way panel/host shutdown and resource retirement from the normal display power callback. Reboot to recover; this experiment does not support display wake. The separate `-wex-dsi-power-test` selects experimental display off/on with the native scanout backing and shared leases retained. It takes priority over `-wex-dsi-stop-test`; system sleep remains unsupported.

### Internal-panel brightness

Add `SSDT-PNLF` to OpenCore to enable internal-display brightness control.

### DVMT

Set **DVMT Pre-Allocated to 96MB** in the BIOS/UEFI settings where possible.

If it is fixed at **60MB**, add `-igfxdvmt` to `boot-args` and set both properties
below on the iGPU under OpenCore's `DeviceProperties > Add`
(usually `PciRoot(0x0)/Pci(0x2,0x0)`).

| Property | Type | Value (hex) |
| --- | --- | --- |
| `framebuffer-patch-enable` | Data | `01000000` |
| `framebuffer-stolenmem` | Data | `0000C003` |

These Data values use little-endian byte order. `-igfxdvmt` corrects the capacity
calculation, while the properties limit the display memory pool to fit within
60MB. Neither setting increases the memory allocated by the firmware.
With 96MB, these DVMT-specific settings are unnecessary.

### Required boot arguments

Add the following to OpenCore's `boot-args`:

```text
-allow3d -disablegfxfirmware
```

| Argument | Purpose |
| --- | --- |
| `-allow3d` | Enable the Apple driver's 3D path. |
| `-disablegfxfirmware` | Disable the Apple driver's graphics-firmware loading path for the current backend. |

The `-wex-nopanel` boot argument has been removed and is no longer needed.
WEX automatically handles systems with or without an internal display.

No family-selection or per-feature enable arguments are required for
Alder Lake-N / Twin Lake.
GPU backend selection is independent of hardware test coverage: untested PCI
revisions use the same family implementation. Revision-specific behavior is
added when a documented difference or observed failure requires it.

Alder Lake-N / Twin Lake and Alder Lake-H/P/U / Raptor Lake-H/P/U select
Y-tiled scanout automatically; `AAPL,GfxYTile` is not required in OpenCore.
Existing values are preserved. Do not use the Apple driver's `-yTileDisable`
argument on these GPUs: their X-tiled scanout path is unsupported.

For framebuffer bring-up, the optional `-wex-noaccel` argument disables accelerator
startup. On Alder Lake-N / Twin Lake and Alder Lake-H/P/U / Raptor Lake-H/P/U,
it also skips GPU resource probing while preserving the
framebuffer's independent boot-memory and MMIO checks. It does not enable an
otherwise unsupported display backend.

Temporary development diagnostic: `-wex-adlp-readout` captures ADL-P boot resources while keeping GPU driver admission disabled. It is not required for supported hardware.
`-wex-adlp-load-only` blocks both native framebuffer and accelerator probes/starts on a selected ADL-P/RPL-P device, for load-only diagnostics without GPU initialization.
`-wex-display13-offline` keeps native display controller initialization enabled on Alder Lake-N / Twin Lake and Alder Lake-H/P/U / Raptor Lake-H/P/U but reports selected outputs as disconnected, isolating HPD/EDID and mode programming for framebuffer diagnostics. It does not establish display operation.

## Tested features and known issues

| Hardware | Verified features |
| --- | --- |
| Tiger Lake / Alder Lake-N | Big Sur–Tahoe: dual 1920×1080 at 60 Hz output, Metal/MPS, OpenGL, OpenCL, hardware H.264 encode/decode, HEVC decode, display sleep/wake, Safari video decoding and Widevine playback |
| Tiger Lake laptops | Big Sur–Tahoe: internal 1366×768 eDP display, brightness control, HDMI and USB-C DisplayPort Alt Mode, up to three simultaneous displays; USB-C reconnection remains unreliable |
| Alder Lake-N | Big Sur/Tahoe: automatic Y tiling without an EFI `AAPL,GfxYTile` setting |
| Alder Lake-H | Big Sur–Tahoe: Metal, OpenGL, hardware H.264 encode/decode, HEVC decode and three displays using fixed HDMI and a DP-to-HDMI adapter |
| Alder Lake-S | Big Sur–Tahoe: Metal, OpenGL, OpenCL, hardware H.264 encode/decode, HEVC decode and three displays (two 1080p60 and one 720p60); Safari video playback; Widevine checked on Big Sur and Tahoe |
| Tiger Lake / Alder Lake-S | Tahoe: Chrome hardware-accelerated Canvas, WebGL and WebGPU |

These results cover tested systems, not every GPU, display, adapter or dock.
The listed resolutions describe test coverage; they are not a fixed resolution
limit imposed by WEX.

### Known issues and limits

- Some image-conversion operations can crash the application.
- App Store video playback can freeze the GPU on Sequoia.
- Reconnecting a monitor to a dedicated DisplayPort connector can leave the
  screen black or freeze the computer. USB-C DisplayPort Alt Mode reconnection
  can also freeze the computer; this has been reproduced on both Big Sur and
  Tahoe.
- Safari video playback may occasionally drop frames.
- Up to three displays can be used simultaneously; a fourth display will not
  receive an image.
- DisplayPort output or reconnection can be slow on Alder Lake-S.
- Some connector layouts, Thunderbolt-managed outputs and panel backlight
  methods are not supported. See [display limits](docs/device-selection.md#display-limits).
- FairPlay-protected playback is not supported.
- Full system sleep/wake and resolutions above those listed above are unverified.
  USB-C display output currently prevents part of the display hardware from
  entering a low-power state.

## Roadmap

- Complete DisplayPort, USB-C and internal-display support on Alder Lake-N / Twin Lake and Alder Lake-H/P/U / Raptor Lake-H/P/U.
- Improve display reconnection reliability and investigate full system sleep/wake.

## Build and test

Use macOS with Xcode command-line tools, Python 3.9 or newer, MacKernelSDK and
Lilu's development headers/library. CI uses the Lilu
[1.7.2 release tag](https://github.com/acidanthera/Lilu/tree/1.7.2),
which includes the Tahoe kernel definition. Place `MacKernelSDK/` and `Lilu.kext/` in the
repository root, or pass their paths with `--sdk` and `--lilu`.

```sh
python3 tools/build.py --release
python3 tools/test.py
```

WhateverXe and AppleIntelTGLCompat kexts and a source manifest are written to
`build/`. TGLCompat source and shared headers are maintained here; its signed
binary is distributed through WhateverXe Graphics. The WEX release ZIP contains
only the OpenCore plugin, not the graphics package. Optional donor replay tests
accept `--donor /path/to/AppleIntelTGLGraphicsFramebuffer` and
`--accelerator /path/to/AppleIntelTGLGraphics`, using the exact binaries accepted
by each test. CPU tests do not establish physical GPU compatibility.
WhateverGreen source is not a build dependency.

## License and credits

WhateverXe retains WhateverGreen's copyright notices and BSD-3-Clause license.
See [LICENSE.txt](LICENSE.txt). Lilu and MacKernelSDK are separate dependencies.

Thanks to [JulioNectar/XPS13Plus](https://github.com/JulioNectar/Hackintosh-XPS13Plus-IrisXe-Tahoe) for sharing its Xe graphics work. Its compiler adapter and LLVM bridge informed the OS-native AIR conversion used in WhateverXe Graphics.

Internal-panel work also draws on [WhateverXe-RaptorLakeP](https://github.com/samranahmad09/WhateverXe-RaptorLakeP), [X1Carbon-G11](https://github.com/dssoftx/Hackintosh-X1Carbon-G11-IrisXe-Tahoe), XPS13Plus and the [Linux i915 display sources](https://github.com/torvalds/linux/tree/v6.12/drivers/gpu/drm/i915/display) for hardware behavior and implementation comparisons.
