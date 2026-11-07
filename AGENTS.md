# WhateverXe

This repository owns WhateverXe, AppleIntelTGLCompat, their shared contracts and
standalone CPU tests. Preserve exact driver and physical identity guards. Keep
refactoring separate from adding support for new hardware.

## Next-release development

Use the current tip of `codex/next-release` as the base for next-release task
branches. Create a separate task branch/worktree; keep the shared integration
checkout available for accepted changes. Before continuing an older task branch,
bring in the current common base while preserving that task's work and history.
Retain its test contracts and maintenance rules when resolving conflicts.
Promoting a change to this base is distinct from release or hardware qualification.
Temporary live-observation code and generated assets do not belong in the base.

## Documentation

README is the user-facing entry point: purpose, current compatibility, required
components, configuration links, build instructions and license. Update those
sections when user-visible behavior changes; do not append implementation or
per-machine test history after each change.
Keep README.md and README_ja.md synchronized in the same change, including
requirements, compatibility, links and examples. Do not add personal names or
private identifying information to user documentation without explicit consent.

Keep setup essentials prominent in both READMEs. Do not append hardware internals,
diagnostic boot arguments or per-change validation details. MIPI DSI needs only
its user-facing scope and limitations in README. Consolidate common regression
coverage and describe only configuration-specific differences in the table.
Preserve concise GPU-family lists grouped as hardware-tested, expected but untested,
further implementation required, and no support planned. Reference a distant table
by its section link rather than saying "below".
Do not add migration instructions for unreleased internal revisions.
Public docs describe current settings and requirements. Keep
investigation notes, implementation diaries, internal diagnostic layouts,
experimental procedures and live machine evidence in the maintainer's private
project documentation, outside this repository. Do not link public instructions
to private files or require access to private history to use or test the source.
Source attribution, licenses and reproducible test fixtures remain here.
Keep release notes in CHANGELOG.md under an exact version heading. Release CI
extracts that section; do not maintain a separate release-note template.

README describes the current state, not release-by-release changes. Recommend
latest component releases instead of accumulating per-version dependency tables.
Use GPU family names rather than internal display-generation labels in README
prose. CHANGELOG describes the affected behavior and OS range; applications used
for validation are not the scope of a general fix. Credit reference repositories
when their work informed a change.

## Build and test

Before implementation or test changes, read and apply the
[contract maintenance rules](tests/README.md#maintaining-test-contracts).
Use independent expectations, preserve valid admission and test production
entry-to-consumer behavior. A target excluded from one handler must reach
another correct WEX handler or native behavior with an explicit compatibility
basis. Unobserved hardware alone is not a rejection reason.
When a test fails, compare both the model and production behavior against that
contract before changing either. Do not change expectations merely to pass.
For test removal, identify where each useful protection remains or document
why the requirement was wrong or retired. Update mode registration and test
documentation with changes; require the modes needed by the affected contract.
Report tested boundaries and omissions before counts. Default CI success does
not establish optional original-input execution, full functionality or hardware
qualification. Apply these rules to existing tests as well as new tests.

Build with `python3 tools/build.py`; run CPU tests with `python3 tools/test.py`.
The builder checks each generated Compat plist, including both DEBUG and RELEASE
in CI. Existing output can be checked with `python3 tools/check_compat_version.py`
and its build directory. Compat's release version and oldest compatible ABI are independent;
do not raise the compatibility floor during an ordinary version bump. An ABI
break requires coordinated Graphics client updates and final-pkg validation.
Lilu.kext and MacKernelSDK are local ignored dependencies. Optional native replay
requires the exact Apple drivers accepted by the tests. Do not commit generated
payloads, Apple driver files, credentials or local machine state.
