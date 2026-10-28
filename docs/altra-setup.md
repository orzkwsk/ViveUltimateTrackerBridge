# Altra direct-HID setup and responsibility boundaries

This console is a standalone bring-up path, not the final product live setup
architecture. It exclusively owns the official dongle; stop `vut_service` and
VIVE Hub / VIVE Streaming Hub first. No process is killed automatically.
Physical Pairing and Identify evaluation for this rework are **HIL_REQUIRED**.

## Pairing, listing and Identify in one session

Run `Setup_Altra_Trackers.cmd` or `vut_setup` with no sidecar argument.
Normal discovery/start/reconnect never sends PairDevice enable.

```text
pair [all|0|1|2|3|4]
pair-status
pair-cancel
refresh
identify
confirm RF_ADDRESS
next
quit
```

After `pair`, physically put one tracker into pairing mode. Default timeout: 60 s.
Feature-response matching confirms command exchange, not successful pairing;
`DevicePaired` is required. The registry sees that event first, then PairDevice
is disabled after the poll callback returns. The dongle stays open for listing
and Identify. `all` uses the existing five-selector payload; a specific selector
only accepts its own slot. Actual first-pair assignment and multi-tracker effects
remain **UNKNOWN / HIL_REQUIRED**.

`vut_pair --slot all --timeout-seconds 60` remains an auxiliary frontend to the
same `PairingController`, printing observed RF data and a diagnostic map suggestion.
Both frontends share begin, event admission, timeout, cancel, result and cleanup.

States: Idle, Enabling, Pairing, Paired, Cancelling, TimedOut, Failed, CleanupFailed.
Pair events must match generation, selector and operation time window. Success,
timeout, cancel, quit, SIGINT/SIGTERM and exceptions attempt disable. Cleanup uses
two bounded attempts; Backend retains one final best-effort attempt before close.
On read loss, retries happen before closing the old handle. Unconfirmed disable
remains CleanupFailed and blocks another enable. Reopen resolves pending disable
before accepting another operation; confirmed enable never carries across transport
generations and reconnect never resumes pairing. A disconnected dongle cannot
be guaranteed to receive cleanup: the tool reports unconfirmed disable explicitly.

## Separate states and responsibilities

Listing separates observed connection, tracking, pose age, stable identity and
body-assignment availability. Observed connection is event evidence, not a heartbeat
guarantee. Paired establishes neither persistent identity nor body assignment.
RF address and slot remain addressing/diagnostic data. Metadata never refreshes pose
age. Unpair/transport loss retires identity bindings; history is bounded to 256 RF IDs.

`StableIdentity` admits verified serial, stable UUID or persistent identifier
provenance. `Devices::observeIdentity` remains the provider boundary;
**production has no verified per-tracker provider and never calls it**. Injected
serial tests do not prove hardware acquisition or automatic restore. RF MAC, slot,
packet index, session and discovery order never become stable identity.

The fixed C1/C2 and inspected Bridge mapping contract place body targets in
MonakaVR. Bridge maps source/device to logical tracker and owns approved spatial
transforms/routing. Cross-backend body assignment belongs to the upper consumer.
No authoritative role enum or body-role apply API is available to this VUT path.
See [Phase 1 audit](altra-setup-rework-audit.md) for exact read-only sources.

The VUT `roles()` list is removed. Normal setup exposes no assign/reassign/roles/save
commands; the launcher creates no sidecar. `Assignments`/`AssignmentStore` are in
`vut::setup::provisional` and a separate internal `vut_assignment_lab`, built only
for tests or explicit `-DVUT_BUILD_ASSIGNMENT_LAB=ON`. Accepted opaque labels must
be supplied by the caller. No built-in body vocabulary or downstream adapter exists.
Retained legacy `schema_version: 1`/`role` fields are private experiment compatibility,
**not a production schema or future compatibility promise**. Existing sidecars are
preserved without automatic migration. An authoritative upper API is required
before implementing a body-assignment adapter.

## Identify evaluation

`IdentifyPolicy` is injected into `Devices`/`SetupSession`. Defaults are
**provisional / HIL_REQUIRED**, retained for synthetic regression continuity:

| Parameter | Default |
| --- | --- |
| Minimum samples / movement increments | 6 / 3 |
| Minimum span / freshness / window | 100 ms / 250 ms / 5 s |
| Accumulated position / angle | 0.12 m / 0.52 rad |
| Position / angle noise floor | 0.015 m / 0.05 rad |
| Position / angle single-step jump limit | 0.5 m / 1.57 rad |

Listing reports sample/movement counts, accumulated position/angle, maximum
single-step position/angle, latest interval, maximum gap and tracking status.
Rejected jumps preserve diagnostic maxima while retiring candidate evidence;
a new Identify window resets diagnostics. Duplicate/reordered RF indices, gaps,
invalid quaternions, unusable state and localization jumps never qualify motion.
Only Ok position and verified RecentlyLost orientation are usable. Multiple
candidates stay ambiguous and require human confirmation. Neither motion nor
confirmation creates stable identity or body assignment.

## Persistence experiment and script errors

The internal store retains corrupt/duplicate/schema fail-closed parsing, unknown
fields, optimistic stale-writer checks, durable same-directory temp writes and
atomic replacement. Writers use a stable adjacent `.lock` path with exclusive
`OPEN_ALWAYS` handle ownership on Windows or nonblocking `flock` on POSIX. OS
process termination releases ownership. The file is deliberately not unlinked;
its existence, including an old sentinel, cannot permanently block saving. Do not
remove it while writers exist, since replacing its inode would split ownership.
An orphan unique temp file does not block later saves.

`vut_setup --offline` opens no HID. Command/syntax/input errors return nonzero even
if later commands succeed. Interactive command errors permit continued operation;
unconfirmed cleanup at shutdown returns nonzero. Internal lab scripts also return
nonzero for malformed input, validation and save failure. The lab is not linked
into normal setup/pair/service/probe targets or exposed by the launcher.

## Future live control and readiness

SetupSession separates the supplied Backend, registry/Identify, shared controller
and SetupConsole presentation. A future service-owned control API can host these
boundaries; this rework does not invent IPC or interrupt tracking automatically.

Persistent assignment and full config-free setup are **blocked by missing verified
per-tracker identity and authoritative upper assignment/apply APIs**. Pairing/Identify
software readiness is assessed independently in the [validation receipt](altra-setup-validation.md).
Hardware tests remain separate from unit/fake-HID/CLI results.

## Receiver presentation and offline GUI evidence

The optional WPF Receiver follows the current Windows UI culture (`ja` selects
Japanese; other cultures select English). Its Language / 言語 control switches
the presentation during the current window lifetime, without writing settings,
restarting the Agent, or issuing control requests. Unrecognized backend messages
and copied diagnostics stay verbatim. Native tray labels remain English.
Stale/unavailable admitted poses show Unknown tracking as well as unknown validity;
historical Tracked state is not presented as current usable tracking.
Cancel enters the terminal Cancelled state only when the cleanup exchange is
confirmed (or unnecessary). An unresolved cleanup remains CleanupFailed and
does not become a successful cancellation. Cancelled describes software cleanup,
not confirmed vendor physical effect. Repeated Cancel does not send another
disable after cleanup is already confirmed.

The test-only `vut_agent_gui_host NEW_DIRECTORY GUI_EXE [--tray]
[--seconds=30..900]` accepts a bounded observation duration (default120s). It uses
real Agent/Job/IPC/tray/WPF with Fake HID, an in-memory revision counter and a
no-op sender; it does not validate physical HID, production journal persistence,
or downstream UDP delivery. `pose-mode.txt` selects full/q/none/unknown/stale.
`control-requests.jsonl` records actual requests/responses, owner polling and fake
HID write count. `stop.flag` performs orderly shutdown; `open.flag` requests a
fixture reopen and is not evidence of a user clicking the real tray. A successful
build or lifecycle harness is not proof of visible GUI/keyboard/DPI acceptance.
