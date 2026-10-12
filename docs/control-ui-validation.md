# Control UI software receipt — 2026-10-13 JST

Baseline: `feature/altra-normal-path-consolidation` at
`29a01978d22c7341f03c1e6f3ab9918851f08a67`. Work: `codex/ui-ux-hardening`.

Windows x64 / Visual Studio 18 2026 / MSVC 19.51 / SDK 10.0.26100.0,
.NET 8.0.424 and SDK 10.0.401, Python 3.13.15, JDK 17.0.20.

| Check | Actual result |
| --- | --- |
| Baseline Release native | PASS 22 distinct tests; blocked sandbox temp/UDP cases passed host rerun |
| `scripts/start_gui.ps1 -BuildOnly -Configuration Debug` | PASS; native owner/service and WPF built; no live process/device started |
| Same launcher, Release | PASS |
| Full final Debug CTest (`build/runtime`) | PASS 24/24 |
| Full final Release CTest (`build/runtime`) | PASS 24/24 |
| Control.Tests Debug and Release | PASS; commands/states, duplicate guard, cancel/timeout, disconnect/invalid/unknown, five devices, persistence failure, shutdown |
| Control.Smoke Release | PASS offscreen five-device WPF layout/binding; screenshot under `build/ui-smoke` |
| Native control framing | PASS malformed/duplicate/version/oversize/large-selector rejection, offline ownership, cancel/close, slow-reader disconnect and worker exit |
| Native setup config | PASS atomic write, installation source preservation, other maps preserved, invalid/corrupt config rejection |
| Release tooling, normal and `python -O` | PASS 13/13 each |
| Production reachability audit | PASS; 47 C++ files, no research recovery references |
| `git diff --check` | PASS |
| Physical/interactive checks | HIL NOT RUN / interactive GUI NOT RUN |

Receipts: `build/control-debug-tests.log`, `build/control-release-tests.log` and
`build/runtime/control-{debug,release}-results.xml`. Initial test/compile failures
were repaired; the final checks above passed. Source-bound release packaging and
physical deployment were not run. No real HID, pairing, SteamVR manipulation,
HMD operation, Hub automation or research command was executed by validation.

Current branch changes only UI/control/lifecycle/configuration/test/documentation
boundaries. Existing parsing, vendor packets, passive ServiceRuntime ordering,
observation/publisher, identity gates and coordinate/revision policy remain intact.
Merge into the selected normal-path lineage is software-reviewable. Hardware
pairing, populated-dongle effects, stable identity, body assignment and live
concurrent service pairing/crash recovery remain separate gates.
