# Altra active reconnect primitives: offline qualification

Classification: **SOFTWARE_QUALIFICATION_PASS**. Date: 2026-10-10 JST.
Source of Truth: the operator-supplied
`MonakaVR_Altra_ActiveReconnect_Primitives_Offline_Codex指示書.md`.
Branch: `research/altra-active-reconnect-primitives`.
Base: `fix/altra-info-epoch-gate`,
`c17b244de64cab728086dafb5bd8acf6d711d6f8`; local/origin base and prepared
ancestry verified. HIL checkpoint `e81ccc3a1db9b9cef33ba9e45f32070005ef433b`
and the official acquisition/reconciliation reports were read-only context.

Saved private official reconnect pcapng was available. Its hash matched both
the acquisition receipt and the previous offline audit. Read-only TShark
re-export matched every existing CSV row. The saved response was independently
extracted from raw frame bytes and matched the previous raw response receipt.
The actual existing C++ RF builder produced both observed requests byte-exact
before the research primitive was added. Original private evidence was unchanged.
Exact capture hashes, payloads, topology and transformation receipts stay private.

| Qualification | Result |
|---|---|
| behavior2 exact structure qualified | true |
| query1e exact request structure qualified | true |
| query1e exact response framing qualified | true |
| official sequence fixture qualified | true |
| private identity leakage | none detected |
| behavior2 / query1e vendor semantic and safety | HIL_UNVERIFIED |
| hardware access / hardware send | NOT RUN |

The pure header [observed_reconnect_wire.hpp](../../include/vut/research/observed_reconnect_wire.hpp)
uses semantic-neutral names in `vut::research`. Behavior code 2 reuses the
unchanged `buildRfBehaviorPayload()` and `buildFeatureCommand()`. Its observed
state byte is fixed and a single valid slot is required; broadcast and invalid
slots fail closed. The query has a literal special request form, distinct from
the generic length convention. The response matcher recognizes only the saved
framing, with optional report ID, and retains its record as opaque bytes.
It does not infer state, identity, readiness, causality or safety. Existing
optional-report-ID recognition and generic command/ACK behavior are unchanged.

The [canonical fixture](../../tests/fixtures/observed_reconnect.hpp) is an inert
projection of the observed requests, response, association boundaries and full
info burst. It preserves the two behavior2 requests around DeviceUnpaired,
the query/response placement before DevicePaired and the nine-token order.
Selector position, all six-byte addresses, incoming packet indices and info
suffixes are synthetic. Boundary/framing bytes remain equal to the raw source
outside the documented substitutions. Background RF/pose traffic and subsequent
host initialization commands are outside this projection. There is no executor,
retry policy or claim that the projected requests are necessary or sufficient.

Production primitive call sites: **0** across Backend, ServiceRuntime, setup,
runner/CLI and automatic actions. No runtime source, protocol implementation,
shared contract, service option or send API changed. CMake adds test targets only.
Static reachability audit passes normally and with `python -O`. In-memory
Backend start/poll/stop tests exercise both ObserveOnly and Legacy, including
existing Legacy writes, and observe zero behavior2/query1e sends. The fixture's
host requests are never passed into a sender. ObserveOnly emits no feature writes
or GetFeature calls. Existing behavior0/1, generic feature and kind-04 ACK bytes
have regression coverage.

Validation: Windows x64 Release configure/build **PASS**; full CTest **21/21 PASS**;
release tooling **13/13 PASS** both normally and with `python -O`. New checks use
explicit `CHECK`, including a deliberately wrong query fixture rejected in
Release (expected nonzero exit). Raw-to-canonical fixed-byte reconciliation,
source-hash preservation, leakage scan and `git diff --check`: **PASS**.
Hardware access, app/service operations, tracker operations and new capture:
**NOT RUN**. No Stage 1, HIL-branch or integration merge.

Next gate: a separate explicitly authorized, single-B, bounded active HIL plan
with individual command permission. This software qualification grants no
permission to send either primitive and does not establish vendor semantics.
