# CPU test contracts

These tests check bounded CPU behavior, dependency metadata and selected driver
instructions. Passing them does not establish GPU execution, physical display,
firmware behavior or arbitrary kernel scheduling.
The same contracts apply to WhateverXe, AppleIntelTGLCompat and their connection;
separate source/bundle locations do not create a weaker quality boundary.

## Maintaining test contracts

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
| `firmware_readout.py` | Intel OpRegion/VBT wire records and owner-bound immutable boot facts: firmware storage may disappear before native image activation, but valid captured wiring must remain available | Production early acquisition and activation consumer on the same XE object, using synthetic HDMI-B/DDC2 records. Physical memory mapping is modeled. Missing input and map failure retry; foreign registry owner, parser-family change and zero owner cannot reuse a snapshot. Semantic mutations that lose a snapshot or reuse another owner's snapshot must fail. Physical firmware lifetime and display output require hardware validation |
| `compat_producer.py` | Explicit board wiring and native port-mode roles: Combo=1, fixed TC=4, DP-alt=3; DSI remains WEX-owned | Real wiring conversion and WEX `xelpBindProbe` construct config delivered to complete Compat, then actual port probe on the same objects. Includes missing AUX/panel/DSI readiness and retry. Hook installation and DSI operation are prerequisites tested elsewhere, not simulated by successful bind |
| `provider_binding.py` | Physical owner/ABI identity, immutable binding lifetime, port record ownership and semantic transport classification | Complete Compat translation unit; preserves physical ID/spoof and ABI1 history, pinning/idempotence and foreign/stale rejection. Replaces a mirrored capability truth table and tuple-bit rejection loop with consumer capability checks and named semantic violations/positive flag variation. Does not model arbitrary kernel scheduling |
| `profile_admission.py` | Physical PCI family classifications from Linux `i915_pciids.h`, supported Darwin families and documented opt-outs; fixed mobile identities also preserve prior selections | Executes the complete production selector using real `Profile` values; checks selected backend/driver and noaccel behavior. Does not qualify physical fuses or native start |
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
