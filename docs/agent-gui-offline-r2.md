# Ultimate Tracker Receiver: Agent GUI software candidate

Phase: OFFLINE_ONLY. The native `vut_service` remains the runtime/HID/config/journal
owner. Optional `--tray --gui=ABSOLUTE_WPF_EXE` adds its Win32 tray and launches one
owned WPF child only when Open is requested. No Windows SCM service or separate
C# tray is introduced. Existing launchers and defaults are unchanged.

The View and ViewModel structure derives from the existing GUI work at
`2cfa10f321b62fad4fc99c690f05d339ef91bab4`. Its independent process launcher,
discovery, stop and config writer are not imported. No other branch is merged.

## Ownership and shutdown

The existing config/journal/port-scoped mutex and suspended child/Job admission
remain authoritative. The native tray owns a separate message-loop thread and
posts bounded Open/Exit requests to the owner. Explorer's TaskbarCreated message
re-registers the icon; failures and an unresponsive tray fail closed. The tray
worker never calls HID. Exit requests runtime shutdown, then confirms owned child
exit before releasing Job/mutex ownership. Existing unconfirmed-child handling
retains Faulted ownership.

Closing WPF closes only the GUI connection; the Agent continues polling. Open
restores only a visible top-level window belonging to the retained child Process.
Hidden helper windows and same-executable other instances are excluded. Open is
refused after stop is requested. Agent process failure closes its Job and owned
GUI. No process is discovered or terminated by executable name.

## Internal control boundary

Agent IPC uses an unpredictable per-process named pipe, one client, bounded frames
(4 KiB requests / 16 KiB responses), owner dispatch and a five-second connection
deadline. Local connections must have the exact spawned child PID. WPF retains
the Agent process and validates the pipe server PID; remote clients are rejected.
Read-only initial `get_status` retries for at most three seconds to tolerate
resume-before-admission scheduling. Pair/Cancel/Identify mutations never retry.
Unknown operations/fields fail closed. GUI busy state serializes actions, and
disconnect/timeout removes stale permissions until a fresh status arrives.

Status comes from the actual ServiceRuntime registry and admitted observation,
not the raw status low nibble. Observed, paired event, valid Pos+Q, Q-only, NONE,
stale and unknown are distinct. RF address/slot remain diagnostic addressing;
stable identity and battery remain unavailable. Publication counts do not prove
Bridge or SteamVR receipt. Identify is passive motion-candidate observation with
provisional thresholds; it is not persistent identity acquisition.

The product configuration leaves `allowExplicitPairing=false`. Concurrent RF
pairing while tracking remains HIL_PENDING. Explicit Pair/Cancel requests run
through the existing backend/PairingController only in an injected fake transport
configuration for software tests. No startup or reconnect initiates pairing.
No Bridge Mapping/World/Mount, body role, IK or output approval moves into this UI.

## Build and repeatable offline checks

Use CMake/Visual Studio x64, JDK 17, Python 3 and .NET 8. Keep outputs in a dedicated
candidate folder. No hardware is needed by these tests.

```text
cmake -S . -B BUILD -A x64 -DBUILD_TESTING=ON -DMONAKA_BUILD_TESTS=ON -DVUT_BUILD_OBSERVATION=ON -DJava_JAVA_EXECUTABLE=JDK17/bin/java.exe -DJava_JAVAC_EXECUTABLE=JDK17/bin/javac.exe
cmake --build BUILD --config Debug
cmake --build BUILD --config Debug --target third_party/monaka-protocol-v2/cpp/monaka_codec_runner third_party/monaka-protocol-v2/cpp/monaka_codec_runner_v2
ctest --test-dir BUILD -C Debug --output-on-failure
dotnet build ui/Control.Wpf -c Release
dotnet run --project ui/Control.Tests -c Release
```

Repeat native build/CTest in Release. The shared codec runners are explicitly
excluded from the default build by their pinned kit and must be built as above.
Native CTest contains the prior 27 cases plus tray, IPC, management and owned-window
tests. GUI management tests use actual Backend/ServiceRuntime with Fake HID and
preserve production decoder/adapter/codec semantics. Existing release-tool mutation
tests remain required, and their native required-case list includes all four additions.

`vut_agent_gui_host NEW_DIRECTORY WPF_EXE [--tray]` is a separate test target, never
a production mode. It uses Fake HID, actual runtime/Job/IPC/WPF, no UDP and a bounded
120-second lifetime. Its fresh directory contains test-only stop/open/pose-mode
controls and local evidence. Do not package or deploy this host.

Physical three-tracker identity/RF, Hub exclusion, real Pair/Identify, sustained
tracking and hardware loss/recovery are NOT_RUN. The candidate is not automatically
installed or enabled. Loaded binaries and physical acceptance need a separately
authorized HIL run; revert by returning to the recorded prior candidate/launcher.
