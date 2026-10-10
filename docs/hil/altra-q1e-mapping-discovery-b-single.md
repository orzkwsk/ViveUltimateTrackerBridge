# Altra Q1E-only single-B mapping discovery HIL

Source of Truth: operator-supplied
`MonakaVR_Altra_Q1E_Only_Mapping_Discovery_HIL_Codex指示書.md` and the request's
independent `ACTIVE HIL GATE Q1E-ONLY` approval. Exact base:
`0c013c1e4ac7c2e3c2340c56e589311b9553380d`.
Branch: `hil/altra-q1e-mapping-discovery-b-single`.

The separate default-OFF `vut_q1e_mapping_hil` research executable reuses the
qualified exclusive HID transport and durable journals. It has no Behavior2
sender or dependency on the prior active session. Its only feature write uses
the unchanged offline-qualified `buildObservedQuery1eReport()`; exactly one
GetFeature follows an accepted write. No retry, replay or automatic vendor action
exists. A shared persistent trial journal and per-run gate intent consume the
single trial before API access, including failure/exception cases.

The targetless host-to-dongle request requires no current slot/RF/address binding.
Physical B-only confirmation, current exclusive transport generation, ten-second
B OFF and thirty-second confirmed B ON baselines, healthy recent capture proof
with zero vendor transmissions, independent human approval and a current one-shot
run/token remain mandatory. A token alone is not human approval. A second tracker,
mutation, registration-loss evidence, unqualified framing or transport loss latches
STOP. Natural fresh connection/info/pose prevents a send. Ordinary successful or
empty-response cases observe at least thirty seconds after API return; unsafe
conditions stop immediately. Response records remain opaque and never create
identity, slot/RF mapping or readiness.

Production sources/APIs/shared contract are unchanged. Research CMake targets
default OFF and are excluded from release packaging. A common HID interface
selection helper was extracted without changing the previously qualified filter.

Software qualification: **SOFTWARE_QUALIFICATION_PASS**. Windows x64 Release
configure/build and full CTest **27/27 PASS**. Release tooling **13/13 PASS** both
normally and with `python -O`. Static reachability/no-retry/Behavior2 exclusion,
targetless eligibility, duplicate/stale/wrong approval rejection, persistent trial
lock rejection, minimum windows and natural-reconnect suppression PASS. Tests use
fake transports; CLI tests only execute help/rejected arguments/stale locks before
the hardware path. Default configure excludes both research executables; leakage,
original raw preservation and `git diff --check` PASS.

Frozen runner SHA256:
`53210bb952921975d550e653bd4eee9b5b116d742a8053aa7f595fc2e02edd08`.
Hardware access/send: NOT RUN. No gate approval, active trial or result is claimed.
Raw evidence and operator receipts stay private. No production/integration merge.
