# Device selection and DeviceProperties

WhateverXe selects the GPU backend from a PCI device table. No CPU model or
machine-name setting is required.

## Automatic selection

By default, the physical Intel GPU device ID selects the backend. The implemented
IDs are Tiger Lake `9A40`, `9A49`, `9A60`, `9A68`, `9A70`, `9A78` and Alder Lake-N / Twin Lake
`46D0`–`46D4`, plus the S/HX and mobile Display13 IDs below.

| Family | Accepted IDs |
| --- | --- |
| ADL-H / P / U | `4626`, `4628`, `462A`, `46A0`–`46A3`, `46A6`, `46A8`, `46AA`, `46B0`–`46B3`, `46C0`–`46C3` |
| RPL-H / P / U | `A720`, `A721`, `A7A0`, `A7A1`, `A7A8`–`A7AD` |
| ADL-S / HX | `4680`, `4682`, `4688`, `468A`, `468B`, `4690`, `4692`, `4693` |
| RPL-S / HX | `A780`, `A781`, `A782`, `A783`, `A788`, `A789`, `A78A`, `A78B` |

Memory configuration can change the physical PCI ID, for example `46A6` to
`4626` on an Alder Lake-H system with one DIMM. Both IDs select the same
family backend without a device-id override. Install matching updated
WhateverXe and WhateverXe Graphics components; the Compat provider also
validates the physical ID. Other added IDs have CPU coverage only.

All PCI revisions use the selected family backend. Hardware test coverage is
recorded separately and does not gate selection. A stepping-specific branch
requires a concrete documented difference or an observed failure. OS, driver
and hardware-resource checks still apply after selection.

Other entries in the table supply identification and display names only. A
listed ID does not imply an implemented backend or a tested machine. See the
[compatibility summary](../README.md#compatibility) for the current scope.

## Advanced override: device-id

To select an existing backend explicitly, set `device-id` on the integrated
GPU in OpenCore DeviceProperties. Its value must be **Data containing exactly
four little-endian bytes**, with a 16-bit device ID and two zero bytes.

For example, Data `D2460000` selects device ID `0x46D2`. This can select a backend
for an unlisted physical Intel ID, but it does not establish hardware compatibility.
The physical PCI identity and revision remain available to hardware checks.
EU counts, memory sizes and connector wiring are determined independently and
are not fabricated by the override.

Malformed values, zero/FFFF, unknown target IDs, identification-only targets and
non-Intel physical devices are rejected. An invalid override does not silently
fall back to the physical ID. Remove an obsolete ICL or other unsupported spoof
to use automatic selection. A restart is required after changing the selection.

- ADL-P/N internal panels: experimental standard eDP on Combo A/B, one panel
  identified by VBT, PPS0 and PCH PWM0. Native modesetting uses the actual lane
  count, link rate, EDID depth and negotiated framing; brightness preserves the
  firmware PWM period/polarity. The firmware must already provide a single
  linear 8-bpc RGB GOP on pipe A. Multiple active firmware displays with an
  internal panel are rejected before bootstrap changes. This does not limit
  subsequent native pipe allocation to pipe A. Use matching source builds of
  WhateverXe and AppleIntelTGLCompat; older providers do not enable this path.
  PPS1, EC/WMI or AUX brightness, DSC/FEC, PSR and startup without a GOP are
  outside this implementation. No ADL-P/N eDP hardware result is claimed,
  including physical brightness, full link retraining or system sleep/wake.

The Display12 small-GT backends (TGL GT1, ADL-S/HX and RPL-S/HX) require
their physical ID without an override. Topology comes from the Gen12 slice,
DSS and EU-pair fuses; the supported four-bank capacity is checked separately.
HX IDs `4688/468B/A788/A78B` select the S-family backend, independent of CPU
branding. RKL remains identification-only: its absent PW_2, PLL/PHY mapping
and HTI resource reservations require a separate Display adapter.

Mobile Display13 keeps the physical family and accepts its physical ID or the
legacy 9A49 donor selector. Other-family overrides are rejected. GT resources
come from slice/DSS/EU-pair/L3 fuses: one slice, up to six DSS, 8/12/16 EUs per
DSS and four/six/eight L3 banks, with supported media-engine fuses. A shared
backend does not imply every resource combination has been tested.

Do not use an override merely to change the displayed GPU name.

## Display name

A user-supplied `model` property is preserved. Otherwise, the name associated
with the physical ID is used; when that ID is unknown, the selected ID's name is
used instead. Names are descriptive labels and do not control functionality or
EU counts. SKU and system configuration can affect Intel's marketing name.
Metal may still report the Apple driver's internal name.

PCI ID/family groupings were checked against Linux v6.12's i915 tables; individual
marketing names are not a claim of independently verified SKU branding.

## Display limits

Connector labels alone do not identify the electrical path. Firmware VBT routing
must match the implemented path; ambiguous or cross-routed AUX layouts are excluded.

- TGL: fixed Combo A/B DP and existing HDMI, eDP and USB-C paths share display
  operations. Additional fixed-DP layouts have CPU coverage, not hardware coverage.
- ADL-S/HX and RPL-S/HX: Combo PHY A–C are implemented; PHY D/E and Type-C/TBT-managed
  routes are not. A USB-C socket wired as fixed DP is different from a Type-C-managed
  path. Firmware-derived DDI A eDP is implemented but not hardware-tested.
- TGL, ADL-S and experimental ADL-P/N panel handling supports the qualified PPS0/PWM0 path. Other PPS
  controllers and AUX-controlled backlights need additional work. Panel timing
  comes from VBT, firmware state and explicit properties. High-rate link selection
  has CPU coverage; not every panel or link rate has been tested on hardware.
- TGL panels requesting HOBL use standard signal settings; the HOBL power-saving
  optimization is disabled until its retry behavior is supported.
- Display 13: fixed HDMI uses firmware Combo A/B and TC1–4 routes; TC requires
  verified legacy mode. Experimental fixed TC1–4 SST DP requires matching VBT
  AUX wiring. Boot handoff supports validated linear RGB scanout, including
  supported firmware scaling, and retires secondary fixed TC firmware outputs
  before native startup. ADL-H has been tested with three outputs and a
  DP-to-HDMI adapter. Direct DP sinks remain unverified. External Combo DP, MST,
  Thunderbolt and cross-routed AUX still need further implementation. Experimental
  TC1–4 DP Alt Mode accepts firmware-described external USB-C DP with matching AUX,
  using the live Type-C ownership, pin and lane assignment. USB-C boot-display
  adoption, hotplug and sleep/wake have not been qualified.

The Display12 small-GT implementation accepts one slice, up to two DSS,
16/24/32 EUs, four L3 banks and the supported media engines. Different resource
layouts need additional implementation. These checks are independent of whether
an individual PCI revision has been tested.

## Tiger Lake GT2 resources

All eight Linux TGL GT2 IDs use the Tiger Lake U/Y Display12 adapter, including
`9A59`, `9AC0`, `9AC9`, `9AD9` and `9AF8`. The last five are implemented but
have no hardware validation. They do not use the Rocket Lake adapter.
Physical selection reads slice, DSS, EU-pair and L3-disable fuses before native
acceleration. Supported URB shapes are one slice with three DSS/48 EUs/six banks,
or five/six DSS with 80/96 EUs/eight banks. Invalid/unavailable readout or a
shape outside these paths prevents accelerator probe/start; framebuffer-only
operation does not depend on the render capacity. The PCI ID does not choose
between the six- and eight-bank paths. Existing explicit donor-override policy
is unchanged; automatic physical selection needs no override.

Linux v6.12's `INTEL_TGL_GT2_IDS` maps to `tgl_info`, the U/Y subplatform and
`tgl_desc`; Gen12 SSEU is decoded from fuses. Combo PHY signal policy already
uses this complete U/Y set. See [Linux PCI IDs](https://github.com/torvalds/linux/blob/v6.12/include/drm/intel/i915_pciids.h)
and [SSEU](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/i915/gt/intel_sseu.c).
