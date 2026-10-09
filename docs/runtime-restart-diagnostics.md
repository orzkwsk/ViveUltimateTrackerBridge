# RR-00B runtime diagnostic observer

`vut_service --version` prints JSON provenance without opening HID, a revision
journal, a socket, or any tracker. Git commit/ref, dirty state and UTC timestamp
are captured by CMake configure from this source worktree. Reconfigure after any
commit or edit before building. `scripts/runtime-build-metadata.ps1 -Executable
build/Release/vut_service.exe` queries that exact binary and binds its SHA256 to
the embedded metadata; it exits 2 unless clean source HEAD and clean embedded
build SHA match. Startup JSONL includes the embedded metadata, with binary hash
null (a binary cannot embed its own final hash).

Runtime output is OFF by default. Add
`--runtime-diagnostic-jsonl=PATH` to the existing service arguments to enable a
separate UTF-8 JSONL file. This flag does not enable the old stderr diagnostic
mode or manual SLAM control commands. Output is an observation boundary around
the actual existing service/adapter, with no wire fields, control commands,
pairing, assignment, map, revision or publication policy changes.

The producer is the single service poll-loop thread. An independent writer
performs all write/flush I/O. A 1024-entry SPSC ring limits memory; pose events use
at most half the queue, leaving transition capacity. Transitions are unsampled.
A separate atomic slot preserves the first pending fatal even when the ring is
full. Repeated fatal events, allocation errors and critical overflow are counted
and invalidate completeness. Worker/write failure never escapes into tracking.
Health counters appear at the existing five-second report boundary and at normal
observer close. `details.dropped_events`, `critical_dropped_events`,
`write_errors` and `observation_complete` expose lost observation. Normal shutdown
drains diagnostics after the tracking loop; a stalled filesystem can delay this
drain, never HID polling. Abrupt process death can lose buffered events. The HIL
adapter must use process exit/stream loss, never infer exit from silence alone.

Identity is `backend_device_id`, full lowercase six-byte RF MAC, scope
`vive-rf-device-address`; it is neither a proven factory serial nor slot/index.
Role is null: this backend has no body-role assignment. Pose validity comes
directly from the already admitted common Observation v2 validity. It therefore
retains the established HIL policy: pre-localization Lost is invalid in both
components, RecentlyLost is orientation-only after numeric checks, and Ok needs
valid position and orientation. `pose_present=true` means an admitted pose sample
was seen, including invalid samples. State packets never manufacture pose
validity or update first-pose observations.

Session ID/generation follow the existing service transport session. First pose
and first position-valid are per full identity per session generation; begin of
each session clears observer history. Discovery reflects backend paired events;
registration reflects admitted state or first admitted pose, and unregister
reflects admitted absent state. The initial `component_ready` is only HID/service
readiness and has `inventory_complete=false`: it cannot establish a complete
tracker inventory. Rejected/unconfigured pair input can be discovered without
implying publication. `backend_tracker_unpaired` retains that raw lifecycle
boundary separately from admitted unregister. No role, remote peer or session
token is guessed.

Host checks: full existing CTest suite plus `runtime_diagnostics` exercise OFF,
ON, identity/null role, validity transitions, generation resets, reserved fatal,
bounded slow-sink pressure, failed writes and extension events. The diagnostic
test executable `--fixture` prints synthetic contract events for external HIL
schema/adapter verification. `--smoke` prints measured OFF/ON idle/ON active
observer CPU, memory, event rate, producer p99/max and writer latency; this is a
synthetic host smoke, not a hardware tracking-latency claim. Fixtures do not open
HID or send any packet.

No production instance is deployed, stopped or restarted by these tests. Real
healthy hardware/provider capture, tracking-loop impact, uninstrumented running
binary mapping and source inventory readiness require a separately authorized
deployment and run. Never use this synthetic fixture to claim hardware readiness.
