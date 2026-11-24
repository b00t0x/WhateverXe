# CPU test contracts

These tests check bounded CPU behavior, dependency metadata and selected driver
instructions. Passing them does not establish GPU execution, physical display,
firmware behavior or arbitrary kernel scheduling.
The same contracts apply to WhateverXe, AppleIntelTGLCompat and their connection;
separate source/bundle locations do not create a weaker quality boundary.

Run `python3 tests/xe_data_access.py <expanded-graphics-pkg>` against a
standard Graphics package expanded with `pkgutil --expand-full`. This follows
the mandatory local dependencies of each OS's actual VA driver and exercises
the production WebKit GPU access decision for their installed paths. The
kernel signing queries are modeled. Safari playback requires a separate test.

## Maintaining test contracts

`tgl_recovery` runs the production wrapper and helper against ABI-matched
reset, scheduler, event-machine, submit and wait callees. The exact TGL donor's
successful non-render branch provides the increment/submit/one-second wait
sequence; only Blit uses it here. For other engines and failed resets the ICL
single-pass ordering and accounting remain mandatory. The fixture checks the
real affected channel, two reload points, raw timeout polarity, no stamp on an
absent affected channel, bounded call counts and published outcomes. Semantic
mutants exercise lost or duplicated work, erroneous gating and failure effects.
This CPU check does not execute the donor's callback bodies, Lilu installation,
IOAccel locking, GPU execution or stamp retirement. The exact donor guards and
an observed live recovery are required at those boundaries.

`tgl_reset_poll_native` executes the production installer, naked bridge and
bounded full-width GDRST helper inside the fixed TGL reset body. It preserves
native scratch writes, forcewake release and context-replay/false exits, checks
the VECS bit above bit 7 and detects timeout, truncation and cleanup mutants.
MMIO acknowledgement, time/forcewake and the scheduler replay leaf are models;
this does not establish electrical reset or GPU progress.

`tgl_recovery_native` takes `--accelerator` and `--ioaccel`.
The accepted Big Sur through Tahoe originals execute eventTimeout, FIFO restart,
diagnosis selection, native stamp resubmission, stamp comparison/deduplication,
fence notifier/list retirement, the real BlockFence client payload callback and
the synchronous finish_fence_event caller. Native IOAccel owns the synchronous wait, its shared
cache writeback, interrupt deregistration and wait-time accounting. The replay
checks both an already-reached target (zero wait time) and a normal arrival
inside the wait (the same duration reaches both reporter channels). It also
checks late fence retirement once and a subsequent normal submission. For Compute, the native watchdog
progress check cancels a stale restart before the engine halt and admits a still-stalled channel.
The exact timeout routine returns the affected channel to FIFO while RESET_CTRL
indicates readiness. The device model clears that hold on an acknowledged
domain reset and retains it on an unacknowledged one. Neither readiness nor
the scheduler returning to the running state delivers a pending client fence.
Compute and media run the donor's beginReset, ring rebuild, updateRingTail,
endReset, descriptor selection and SQ/LOAD stores into RAM. The output records
both the command producer's tail and the context image tail after restart.
Dropping LOAD or clearing the force-restore flag must fail before any stamp
is supplied. CPU submission does not establish GPU execution.
The notification cases take hardware stamp arrival as an independent input,
using the address and value decoded from Compute PIPE_CONTROL or media
MI_FLUSH_DW. Kernel scheduling/locks, VM mappings, forcewake, interrupt
delivery, diagnostic report publication, idle accounting, DMA and normal
scheduler enqueue capacity remain modeled. An observer bounds the native
pending-wait case in a child. A permanently missing stamp can leave callers
waiting. The retained state does not establish recovery, safe teardown or idle.

Apply these rules to implementation changes and test additions, corrections,
merges and removals. An existing passing test is not an approved specification.
The [check inventory](#independent-expectations-and-boundaries) describes selected
checks; it is not certification of every assertion or of the entire suite.

Before a change, identify the intended behavior and retained behavior, then
choose the production paths and tests needed to protect them. Keep a short
description near the test, in this document or in the change description:

| Record | What to identify |
| --- | --- |
| Contract and basis | Accepted capabilities, required effects, owner/order/failure rules; specification, reference implementation, exact donor caller/consumer or immutable positive history |
| Tested path | Production entry, input producer, state propagation and consumer; explicitly modeled prerequisites and hardware/OS boundaries |
| Counterexamples | Invalid rejection, missing work, wrong delegation, duplicate effects or another plausible error that must fail; valid inputs and meaning-preserving variations that must still pass |
| Execution and limits | Required modes/inputs, tested source and result locations, omitted paths and conclusions the check cannot support |

Reuse an existing description when it still applies. Do not create a separate
form for every assertion or copy implementation conditions into an oracle.
Synthetic cases need a contractual basis; retained readouts need provenance.
Capacity, bit position, OS metadata and layout should vary independently where
the consumer contract allows it. Previous positive inputs remain independent
protections; previous negative inputs must not freeze an admission limit.

WEX owns the required adaptation for target Xe-LP devices, including reused
native paths. A branch declining work must reach another correct WEX handler
or a native path expected to work for a stated reason. Preserve shared handling
for the same contract and split confirmed differences. Test both responsibility
gaps and wrong backend selection. Mismatch with an observed configuration,
resource tuple, build metadata or fixture size alone is not a rejection reason.
Preserve actual ownership, input-safety, ABI and fixed-donor identity guards.
If a required operation or ABI cannot be established, keep the corresponding
consumer unready and record the missing capability. Refusal is not completed
support for that capability.

Use real production code for the dependency decisions and state propagation
being tested. A direct helper check does not prove its caller reaches it.
An explicit `true` prerequisite is still a model assumption: identify how the
real producer establishes it and test the connection where required. Do not
auto-enable fields or dependencies discovered in production source. Check the
effect at the consumer, including failure, retry, reentry and missing events
where relevant. A successful return or internal ready flag need not mean that
the requested work occurred. For shared notifications, model native ownership
and delivery as well as WEX calls.

On failure, compare both the test model and production against the independent
contract. Fix the incorrect side, which can be both. Distinguish fixture API
updates from changes to expected behavior; record the basis for the latter.
Do not delete a valid input, add a success stub or skip a mode to make a known
defect pass. Do not restore an obsolete candidate policy merely for an old test;
establish whether its original requirement was valid and where it is protected.
If the contract remains unresolved, keep that uncertainty visible.

Before deleting or merging tests, identify each useful contract, boundary and
counterexample and where its protection remains. Move unique protection first.
Keep useful structural lint labeled as such; neither test counts nor source
spelling can substitute for behavior checks.
For important contract changes, demonstrate a relevant semantic fault failing
at the intended behavior assertion. Reuse suitable existing counterexamples;
extraction/compile failures are not evidence of detecting the intended defect.
Do not require a global mutation score or a new framework.

Register new scripts and modes in `tools/test.py`, with distinct job names,
required inputs and correct kinds. Shared helpers need registered callers.
Run the modes required by the affected contract with `--require`; the default
CI command does not supply optional original inputs. Registration completeness
does not prove requirement coverage. Keep per-run failures and omissions,
including when a later run resolves them. Combine complementary runs only when
their source, fixtures and input contracts correspond; do not select passing
fragments to hide unresolved failures. Update the relevant descriptions when
contracts, boundaries or modes change. Report those boundaries, required-mode
results and limits before aggregate counts, and keep build and hardware claims
separate from CPU assertions.

## Selecting and requiring checks

```sh
python3 tools/test.py --list
python3 tools/test.py --only profile_admission --only display13_power_dispatch
python3 tools/test.py --accelerator /path/to/AppleIntelTGLGraphics --only tgl_gt2_urb_native --require tgl_gt2_urb_native
```

`--list` includes modes whose inputs are absent. `--only` is repeatable and
accepts shell-style patterns; quote patterns to keep the shell from expanding
them. `--require` also selects its matches and makes missing inputs a failure.
Default CPU checks can succeed with optional binary checks skipped. The result
records those omissions; that success must not be described as binary replay.
The `binary-check` kind means an original input was supplied. Individual
checks may validate bytes, execute selected instructions, or both; it does not
mean that the complete original driver executed.

Each invocation creates a new directory under `build/tests/run-*`, or uses the
new directory supplied with `--output`. Each job has its own `test.log`.
`results.json` records passed, failed, skipped and not-selected jobs. Core C++
jobs with the same stem as a Python adapter have an explicit `_cpp` suffix;
both run and neither result overwrites the other. `fully_executed` refers only
to this job inventory, never to hardware/function coverage.

Compile and execute stages are distinct for direct C++ jobs. Python replay jobs
report a script stage; their inner extraction/compiler/runtime diagnostics are
in the log, rather than being classified from a return code alone.

The original Apple inputs are optional, local and untracked. Use the exact
inputs accepted by each replay. Kernel and host modes also need Lilu HDE via
`--hde-source`. Do not put Apple driver files in this repository or public CI.
Additional modes declare their inputs explicitly: `--linux-tables` accepts
`intel_ddi_buf_trans.c` for the DKL coefficient oracle; `--ccs-records` accepts
retained JSON `records` with `after.aux` and sixteen `after.words` for an aux-state
reproduction; `--pcode-donor` supplies the distinct original required by the
eDRAM diagnostic. Its audit copy stays in that job's output and is not deployable.
The outer-ring check uses current production source unless `--outer-source` is
explicitly supplied. These input identities do not restrict runtime OS updates.

Every standalone Python script has an enumerated mode. `production_source.py`,
`pmap_kernel_contract.py` and `compat_test_support.py` are shared helpers exercised by registered callers.
The registry contract checks that newly added scripts are not silently omitted.

## Independent expectations and boundaries

| Check | Expectation and source | Boundary and exclusions |
| --- | --- | --- |
| `compat_exports.py` | Fixed `c004fc1a` donor imports, native DSB caller/field producers, mapped-memory extent, contiguous Gen12 PTE address encoding, DSB manager dispatch and zero-success return | Compiles the complete Compat translation unit with kernel/IOKit boundary mocks, retaining export asm names. Executes MMIO, DSB allocation/zeroing/PTE installation/publication, failure/retry, clock and lifecycle. Hardware DMA, the system mapper and actual DSB queue execution are outside this check |
| Graphics activation (`compat_exports.py`, `host_publication.py`) | Either component alone must preserve firmware graphics; native drivers may start only with an active, compatible WEX plugin. Activation ABI 1 is a runtime registry token, independent of EFI/AuxKC linking | Executes the actual accepted pluginStart body, token publisher and complete Compat start consumer. Missing, malformed and incompatible tokens reject start. The early callback may copy read-only firmware facts after PCI publication; it must not initialize/configure the GPU. Actual host dispatch ignores unrelated notifications until a Graphics image arrives, then initializes once. Lilu's shouldLoad decision, OSKext dependency failure propagation, AuxKC construction and GOP scanout require separate loader/hardware validation |
| `firmware_readout.py`, `firmware_panel_identity.cpp` | Intel OpRegion mailbox 5 and Linux v6.12 `intel_bios.c` BDB 41/42 PNP identity: exact-date match precedes an undated match; no blind profile-zero fallback. Identical child records have the same ownership; conflicting records do not | Actual early acquisition, inline/external VBT, owner cache, wiring, PPS delay and PWM consumers. Explicit indices and LFP2 are retained. Equivalent consumed facts remain usable without claiming an index; divergent candidates, malformed pointers/EDID and conflicting children cannot supply panel routing. Mapping and MMIO are modeled. Snapshot loss, foreign ownership and disconnected EDID mutations fail. Physical mailbox availability and output remain hardware boundaries |
| ADL-S initial panel mode (`adls_panel_pwm.py`) | Fixed donor reset starts Combo mode0 before mode1 publication. The bound internal panel, controller/access association and PG1 witness independently establish PWM ownership | Production LightUpEDP wrapper captures PWM in mode0/1, then delegates once. Native mode checks the reset producer and executes the PWM tail for the first mode0 cycle and subsequent mode1 cycles. Restoring mode1-only admission fails semantically. No claim of a complete ADL-S cold engine reset or physical panel training |
| Deferred Display13 clock (`display13_training_frontend.py`) | Exact donor stop caller defers clock removal until DDI buffer/transcoder stop. Reentry with the same owner/value is the same pending operation | Actual deferred request and stop consumer with the real power bridge and mocked MMIO. Identical repetition stays pending without a fault; conflicting values stay suppressed without overwriting the request; running DDI prevents clock removal. Physical disable timing remains outside the model |
| `compat_producer.py` | Immutable ABI3 binding and port modes from the exact donor/Compat contract. Legacy ABI3 rejects a nonzero Alt mask | Real WEX bind, port state, direct HAL mode and both HPD dispatchers. A mixed eDP/TC board retains its panel path when a TC hook is absent and uses the same pinned provider after the TC path becomes available. A pre-Alt provider gets the representable subset. Complete Compat is executed. Native state/HPD leaves, hook installation and hardware are boundaries |
| `display13_tiling.py` | Shared IRQ/power/clock and multi-output retirement precede native startup. The firmware transcoder identifies the boot output | Actual start prelude, provider properties and deferred boot selector. A missing TC hook permits eDP boot and blocks fixed TC boot before TC capture; restoring it reaches the existing handoff. DP-alt GOP stays unqualified. Readout, final boot-memory preparation and native start are boundary models |
| Display13 boot link depth (`display13_linear_dp.cpp`, `display13_fb_memory.cpp`, `display13_boot.cpp`) | Linux v6.12 `i915_reg.h` TRANS_DDI_BPC encodes 8/10/6/12 bpc as 0/1/2/3; lane width is a different field. The four-byte RGB plane does not require an 8-bpc output link | Actual source readout, plane conversion, GGTT backing inspector and multi-output parking retain the firmware depth. Reserved depths fail before writes/PTE reads; drift is refused. This preserves a firmware link, not HDR or native deep-color modeset support |
| `display13_boot_handoff.py`, `display13_training_frontend.py`, `display13_sst.py` | Fixed donor `resetSoftwareState` initializes Combo mode to 0 before `setPortMode`; exact physical config, access owner and TC modes remain required | Production setup-workloop -> SST selection -> boot AUX adoption -> port stop/re-enable, and training wrappers. Prequalified framebuffer memory and provider IRQ registration are boundaries. `display13_sst_native` checks producer bytes and replays existing native port operations; full native startup and physical link training are outside the model |
| `display13_boot_handoff_native`, `display13_boot_resource_native`, `dsi_aperture_adapter` | Same wired port, matching or incompatible stream. Same GPU base, initial linear or subsequent tiled owner. Partial last page must belong to the pool and BAR. | Workloop adoption, actual mode decision, donor comparator and plane setup, accelerator layout producer and destroy, exact-length descriptor and balanced references. `dsi_aperture_adapter_native` returns the actual stolen-pool slot with a peer slot active and a client still retaining the descriptor. AUX replies, allocation and register transitions are modeled. Physical panel and WindowServer output need separate evidence. |
| `display13_boot_handoff` and `display13_boot_handoff_native` | Native DSC teardown can skip the sink or ignore a failed write. The next plain eDP link needs decompression off before training. An unreadable DSC register permits writing zero only on a direct panel, with the source stopped and an AUX ACK. | Physical Combo-panel adoption owns the next stopped plain-link DSC check independently of fastset eligibility. Source expiry, config/lock/alignment loss, AUX failure, ownership loss and a port stopped before comparison retain that duty. A stable firmware CMTG A secondary on a single internal eDP A may remain enabled when the source scanout and DSC blocks have stopped and the receiver positively advertises no DSC. Tests retain the stop requirement for an unknown or DSC-capable receiver. A fresh post-stop capability read prevents optional DSC access on non-supporting sinks; unknown identity refuses training without writing. Native DSC training retains its own consumer and the plain-link duty. The full-mode decision reaches the production training wrapper. Native mode also replays the final connector dispatch in `hwSetMode`, patched `LightUpEDP`, `disableDisplay`, sink DSC writes, and the native port and Combo clock bodies through their production adapters. It checks failure before scanout, ordered shutdown, restored power reference counts and clock allocation, a live peer PLL, and retry through the scanout and brightness calls. The fixture models MMIO completion, power callback environment, AUX replies and electrical training. |
| `display13_clock_runtime.py` | Cold CDCLK has no live consumers; all powered planes/cursors/DSB/transcoders must be stopped under the display owner lock and IRQ guard. A caller-owned FB mutex must not wait for a potentially dependent workloop gate | Actual dual-clock dispatcher, IRQ guard and clock state machine with MMIO/mailbox/locks modeled. Gate contention, clock publication, no-write refusals and partial-write IRQ containment are checked. Active-clock success certificates and real scheduler races are outside this fixture |
| `display13_panel_runtime.py` | Fixed donor `LightUpEDP` powers the panel before clock setup/training. Linux `intel_pps.c` and CNP PWM use PCH rawclk, independent of CDCLK | Actual panel power/brightness wrappers run with CDCLK stopped or running, retain PG1 and owner/fault guards. Native mode replays patched `LightUpEDP` into the real power wrapper; clock/training leaves are mocked there. Physical PPS timing, brightness and panel link remain unqualified |
| `provider_binding.py` | Physical owner/ABI identity, immutable binding lifetime, port record ownership and semantic transport classification | Complete Compat translation unit; preserves physical ID/spoof and ABI1 history, pinning/idempotence and foreign/stale rejection. Replaces a mirrored capability truth table and tuple-bit rejection loop with consumer capability checks and named semantic violations/positive flag variation. Does not model arbitrary kernel scheduling |
| `profile_admission.py` | Physical PCI family classifications from Linux `i915_pciids.h`, supported Darwin families and documented opt-outs; fixed mobile identities also preserve prior selections | Executes the complete production selector using real `Profile` values; checks selected backend/driver and noaccel behavior. Does not qualify physical fuses or native start |
| `xelp_video_irq_native` | Gen11+ shared bank1 DATA_VALID/W1C and locked bank snapshot (Linux v6.12 `intel_gt_irq.c`); fixed TGL donor event tables and class-enable mask writes | Installs the production parent bank owner, executes the exact native `processInterrupts` and `serviceInterrupts`, and runs the production timer/lifecycle wrappers on boundary models. Checks VCS masks 1/4/5, VECS and bank0 peers, delayed/stalled/foreign identities, late peer dispatch, native fuse-driven enumeration rejecting an absent foreign consumer, bounded quarantine/retry, mask restoration, timer delivery after detach/rebind, a retry crossing stop during forcewake, late bank0/bank1 IRQs during disable, and installation failure. Semantic mutants detect lost obligations, wrong or absent consumers, missing ACK, an unmasked storm, lost retry delivery, a disabled timer after rebind, retry/IRQ admission after stop, and acquisition of the native accelerator gate during lifecycle. The latter is forbidden by the native stop/GART-collector lock order; retry uses a private WEX loop. Lilu byte routing, electrical IRQ/mask behavior, workloop scheduling and GPU waiter/recovery results require hardware qualification; no completion is inferred from a timeout. |
| `adlp_video_route_native` | TGL GT2 (48 EU) and ADL-P/U (64 EU), both 6 L3 banks, with VCS masks 1, 4 and 5; invalid fuse words and mismatched donor bytes | Uses the production GT readout, admission and patch installer with a bounded Lilu stand-in. Runs original x86 blocks from the media fuse load through enumeration, system/media selection, ring preparation, scheduler binding and EL descriptor stores into RAM. Allocation, interrupts, stamp retirement and hardware execution are outside this replay. |
| `gen12_resource_cases.hpp`, `adlp_resources.cpp`, `tgl_gt2_urb.py` | Positive resource capacities and Gen12 SSEU bit meanings; Intel TGL PRM Vol. 4 and Linux `intel_sseu.c` | Separate EU/DSS/L3 populations and position permutations; native URB stores use independent expected capacities, including eight native subslices. Unlisted encodings are not automatically negative specifications |
| `accelerator_resources.py` | Explicit capacities, forcewake ownership/readout prerequisites, resource failure visibility and refusal of native probe | Complete production profile selection, qualification, reporting and probe on the same object. Readout acquisition and other installers are explicit fixture prerequisites; their producers and the remainder of native start are outside this check |
| `host_publication.py` | Loader callback deadline, actual consumer dependency roles, IOSurface getter fields and consumer-specific use | Production host dispatch plus real surface ABI setter. Includes missing notifications, long valid lists, caller read windows and independent getter corruption. Other installers and symbol lookup are modeled boundaries |
| `display13_power_dispatch.py` | Fixed framebuffer donor `c004fc1a`: Sleep exit owns DidWake, Wake entry owns DidPowerOn, Doze exit has no DidWake | Production native power wrapper, ownership check and common notifier together; every subscriber state/order without DSI. Hardware restore and donor leaves are modeled, not a kernel scheduler |
| `display13_wake_notifications.py` | The same donor notification ownership, subscriber paging activation, retry and reference balance | Direct notifier behavior. Its no-DSI case alone is not proof of production reachability |
| `dsi_update_dispatch.py` | DSI wrapper serialization, real notification completion, delivered paging state and retry without a second panel restart | Executes the real wrapper and notifier together. Panel stop/resume and shared restore generation are boundary models; notification success without delivery remains incomplete |
| DSI framebuffer depth (`dsi_presentation.py`, `dsi_native_mode.py`, `dsi_apple_adapter.py`) | Fixed donor `validateModeDepth` maps indices 0/1 to four-byte RGB8/RGB10; `getPixelInformation` supplies independent component masks and native `configurePlane` selects the corresponding plane format | Production mode request through presentation, current-mode/pixel callbacks during allocation, depth-only changes, transformed source reallocation and failed-change rollback. Detailed validation and mode enumeration expose maximum depth index 1; panel timing stays RGB8. Native mode requires `--donor` to replay real pixel descriptors and plane configuration; memory/string, scaler/color and packet-submission leaves are mocked. Actual CoreDisplay selection, GPU conversion, MMIO latch and dithering require hardware validation |
| `framebuffer_common.py` | Common installation precedes backend/native start; incomplete installation prevents native start; repeated entry does not add trampolines | Executes installers/start gate, including partial failure. Source spelling or symbol occurrence counts are not used as behavior expectations |
| `display_contract_adapter.py` | Explicit fixture capabilities and route-specific missing operations | Dependency bookkeeping, not proof that every capability producer is connected. A new production field cannot be silently auto-enabled |
| `released_physical_admission.py`, `released_topology.py` | Immutable earlier release inputs and assertions | Retains earlier positive selections/caller behavior independently of the current table. Does not make earlier negative inputs permanently unsupported |
| `test_runner_contract.py` | Missing required work fails, all modes are visible, failure stops execution, results cannot be overwritten | Exercises the actual runner's registration, omissions and stage handling |
| AUX parent/observer replays | Reference ownership, inaccessible MMIO and memory-only telemetry retention, including quiet training | Old fixture declarations and qualification have been updated without changing the reference-balance expectation. Added PG telemetry is not required to occupy old record indices |
| DKL and outer-ring replays | Linux coefficient tables and Gen12 PIPE_CONTROL bit meanings, respectively | Actual donor code runs with boundary leaves. Outer-ring comparison uses original bytes as baseline, not a candidate with one bit cleared |

Compat's fixed donor identity is
`c004fc1ac9ee4157e2e12736f40fa6639bde025a279326beeb9dd48c9b445731`.
Its `FBMemMgr_Init` at `0x42e18` produces the 18-page DSB size, independently
of observed machines. Native start at `0x3f903..0x3f938` produces a 1/8 MiB
PTE window at BAR0+8 MiB and records its extent at controller+CEC. The DSB
caller at `0x4050f..0x40534` requires zero success and then maps the requested
GGTT subrange. Tests check every PTE and neighboring word, zeroed backing,
publication after validation, failure release and retry. The final complete
word belongs to a mapping; the donor's own off-by-one comparisons are not an
oracle overriding that memory extent contract.

Probe binding's 2/16/32 MiB variations test an honest map/manager pair, not new
native hardware support. Per-register read bounds determine probe readiness;
the observed 16 MiB map size is not a probe ABI requirement. The donor's nine
records and fixed class-layout offsets remain ABI guards. Generic Display12
register/DSB access remains separate from Display13 binding. Explicit-base
64-bit access preserves native manager dispatch for MMIO and direct stores for
other bases; the caller still owns the external allocation's actual extent.
The builder checks defined global symbols against the donor's eight retained
imports and the current/ABI1 WEX binding exports, as well as the existing bundle compatibility floor. Synthetic plist
unit cases alone do not establish loadability or full ABI correctness.

The resource fixture distinguishes a retained raw readout from a synthetic
encoding of a capacity. A patched driver field is not a physical fuse value.
Resource count formulas copied from the decoder are not acceptance oracles.
Sanitizers, explicit invalid-input checks and positive history remain separate
protections; removing a mirrored acceptance formula does not remove them.
The obsolete L3 data-bypass candidate check was removed: it required a policy
that is absent from current production. Unreferenced transmitter fixture copies
were also removed; current transmitter, mode, PHY-gating and AUX-lifetime replays
retain those active contracts. Neither deletion changes a runtime admission rule.

Useful primary references include
[Linux v6.12 SSEU](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/i915/gt/intel_sseu.c)
and [PCI classifications](https://github.com/torvalds/linux/blob/v6.12/include/drm/i915_pciids.h).
These sources specify hardware facts, not Apple private ABI compatibility.

### Display13 deferred IRQ, GGTT resume and AUX backlight

`display13_irq_rearm` executes the production enable/disable/VBL wrappers and
real IRQ bank logic. The donor calls VBL after PIPECONF (0x49193/0x491ad).
Disabled pipes defer without permanent fault, then rearm on actual activation;
quiescing, blocked pipes and foreign owners cannot arm interrupts. Association
validation and kernel lock primitives are modeled leaves, not GPU IRQ timing.
Combo DP/HDMI, TC DP-alt, eDP plus external, and DSI plus external also resume
in two stages across pipes A/B/C. A later peer must preserve pending pipe/DSB
completions and peer masks; both native lock conventions execute. Replacing
incremental activation with a global rearm must fail at that external effect.
`display13_shared_restore` executes the actual shared restore wrapper and stopped
consumer checks. Power-well restore/publication are modeled leaves. Linux v6.12
`intel_dpt_configure` specifies GGTT selection. No engine enable may occur after
unreadable/failed GGTT readback or with active consumers; pending state survives.
Retry keeps the lost-power generation for normal and DSI transcoder bindings.
A retained, running engine accepts peer wake without reprogramming GGTT or
re-enabling the engine, even when a new GGTT transaction would fail.

`display13_panel_runtime` now covers VBT-selected VESA AUX, the PPS wrapper,
brightness/enable/disable callbacks and real PG/AUX leases. Native AUX replies
and serialization are modeled; `display13_aux` separately tests the transport.
The native mode executes `processDisplay13Panel` against the exact donor and
replays its generated period-call bridge in LightUpEDP, with the real outer
LightUpEDP wrapper and health result. It retains original-donor counterexamples,
checks partial installation/mismatched bytes, and restores a lost period on the
existing PWM path as well as exercising AUX. Common native panel installers
remain explicit prerequisites; patcher memory writes/routes and OS locks are
modeled. The test must not assemble a substitute period bridge.
Linux DRM eDP capability, byte ordering and PWMGEN bit count define the level
oracle. 8/16-bit sinks, AUX/pin enable, zero PWM availability, transaction failure,
foreign owner, power-off and restoration preserve normalized brightness without
writing absent PWM registers. `display13_panel` retains PWM-only callers and
adds AUX panel wiring for Combo A/B across Display13 families. Its former
PWM-absent rejection becomes a positive AUX-probe case; unknown VBT backlight
facts remain rejected. OLED-specific nits calibration and physical panel
brightness are not demonstrated by any CPU mode. Require these modes and the
native panel/AUX modes when changing this path.

Shared firmware identity checks also run inline/external capture and cached facts
for TGL, ADL/RPL-S, ADL/RPL-P and ADL-N. Consumers follow their physical mapping:
TGL internal eDP stays on the native panel route, and ADL-S HDMI-B is TC1 rather
than Combo-B. Optional panel damage must preserve external HDMI wiring. The
ADL/RPL-S PWM replay preserves level/polarity and native non-panel delegation;
entering a Display13 helper in that fixture fails. These are CPU routing and
lifecycle protections, not new Display12 AUX support or hardware qualification.

For non-eDP regression isolation, `display13_panel_runtime_native` additionally
calls the real eDP installer with DP-only, HDMI-only and DSI-plus-external wiring
while stale panel firmware facts remain present. Neither donor bytes nor common
panel hook setup may change. Removing the non-eDP guard must fail at this no-write
boundary. The existing Display13 PWM branch also makes no AUX calls and preserves
the modeled sink. `profile_admission` and `framebuffer_common[_native]` separately
protect Display12 backend selection and installation dispatch; their combination
is not a full kernel startup replay.
