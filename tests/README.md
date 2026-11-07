# CPU test contracts

These tests check bounded CPU behavior, dependency metadata and selected driver
instructions. Passing them does not establish GPU execution, physical display,
firmware behavior or arbitrary kernel scheduling.

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

Every standalone Python script has an enumerated mode. `production_source.py`
and `pmap_kernel_contract.py` are shared helpers exercised by registered callers.
The registry contract checks that newly added scripts are not silently omitted.

## Independent expectations and boundaries

| Check | Expectation and source | Boundary and exclusions |
| --- | --- | --- |
| `profile_admission.py` | Physical PCI family classifications from Linux `i915_pciids.h`, supported Darwin families and documented opt-outs; fixed mobile identities also preserve prior selections | Executes the complete production selector using real `Profile` values; checks selected backend/driver and noaccel behavior. Does not qualify physical fuses or native start |
| `gen12_resource_cases.hpp`, `adlp_resources.cpp`, `tgl_gt2_urb.py` | Positive resource capacities and Gen12 SSEU bit meanings; Intel TGL PRM Vol. 4 and Linux `intel_sseu.c` | Separate EU/DSS/L3 populations and position permutations; native URB stores use independent expected capacities, including eight native subslices. Unlisted encodings are not automatically negative specifications |
| `accelerator_resources.py` | Explicit capacities, forcewake ownership/readout prerequisites, resource failure visibility and refusal of native probe | Complete production profile selection, qualification, reporting and probe on the same object. Readout acquisition and other installers are explicit fixture prerequisites; their producers and the remainder of native start are outside this check |
| `host_publication.py` | Loader callback deadline, actual consumer dependency roles, IOSurface getter fields and consumer-specific use | Production host dispatch plus real surface ABI setter. Includes missing notifications, long valid lists, caller read windows and independent getter corruption. Other installers and symbol lookup are modeled boundaries |
| `display13_power_dispatch.py` | Fixed framebuffer donor `c004fc1a`: Sleep exit owns DidWake, Wake entry owns DidPowerOn, Doze exit has no DidWake | Production native power wrapper, ownership check and common notifier together; every subscriber state/order without DSI. Hardware restore and donor leaves are modeled, not a kernel scheduler |
| `display13_wake_notifications.py` | The same donor notification ownership, subscriber paging activation, retry and reference balance | Direct notifier behavior. Its no-DSI case alone is not proof of production reachability |
| `dsi_update_dispatch.py` | DSI wrapper serialization, real notification completion, delivered paging state and retry without a second panel restart | Executes the real wrapper and notifier together. Panel stop/resume and shared restore generation are boundary models; notification success without delivery remains incomplete |
| `framebuffer_common.py` | Common installation precedes backend/native start; incomplete installation prevents native start; repeated entry does not add trampolines | Executes installers/start gate, including partial failure. Source spelling or symbol occurrence counts are not used as behavior expectations |
| `display_contract_adapter.py` | Explicit fixture capabilities and route-specific missing operations | Dependency bookkeeping, not proof that every capability producer is connected. A new production field cannot be silently auto-enabled |
| `released_physical_admission.py`, `released_topology.py` | Immutable earlier release inputs and assertions | Retains earlier positive selections/caller behavior independently of the current table. Does not make earlier negative inputs permanently unsupported |
| `test_runner_contract.py` | Missing required work fails, all modes are visible, failure stops execution, results cannot be overwritten | Exercises the actual runner's registration, omissions and stage handling |
| AUX parent/observer replays | Reference ownership, inaccessible MMIO and memory-only telemetry retention, including quiet training | Old fixture declarations and qualification have been updated without changing the reference-balance expectation. Added PG telemetry is not required to occupy old record indices |
| DKL and outer-ring replays | Linux coefficient tables and Gen12 PIPE_CONTROL bit meanings, respectively | Actual donor code runs with boundary leaves. Outer-ring comparison uses original bytes as baseline, not a candidate with one bit cleared |

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
