# VIVE control frontend

Run `Start_GUI.cmd` on Windows with Visual Studio C++/CMake and the .NET 8 desktop
SDK installed. It builds the native owner and WPF frontend, then opens one window.
No HID interface is opened and no pairing command is sent just by opening it.

## Normal setup

1. Stop a tracking service through its existing launcher, or use **Stop tracking
   service** if this window started it. Close VIVE Hub / VIVE Streaming Hub.
2. Click **Connect setup**. The native setup owner opens the dongle exclusively
   using ObserveOnly policy. An ownership/open error suggests the next action;
   no competing process is killed and there is no automatic reconnect.
3. Click **Pair**, then put one tracker into pairing mode. **Cancel pairing**
   cancels explicitly; the operation times out after 60 seconds. A paired event
   is association evidence, not stable identity, tracker role or vendor-mode
   confirmation. Disable cleanup remains bounded best effort on all exit paths.
4. Check devices and tracking. On **Advanced**, select the observed RF address,
   enter its actual native spatial map ID, and **Save observed RF / map**.
   This writes the existing source/map admission format atomically and preserves
   the installation source and other maps. It creates no stable device identity,
   body role, upper-layer approval or calibration. Corrupt files are preserved.
5. **Close setup**, then **Start tracking service**. The existing ServiceRuntime,
   adapter, publisher, strict/preserve revision policy and journal remain owners
   of normal tracking. The GUI does not enter concurrent service-owned pairing.
   Output/profile approval and body assignment remain in Bridge / MonakaVR.

An observed RF address is a configuration key, not persistent hardware identity.
If that address or the native map changes, explicitly update the configuration
and restart tracking; automatic restoration is unavailable. Selectors `0..4` are
vendor selectors. Their slot semantics and populated-dongle effects are still
UNKNOWN / HIL_REQUIRED. The default selector is all; it is not a body part.

## Architecture and lifetime

WPF displays bindings and commands. `ControlViewModel` owns polling, busy state,
validation feedback and command availability. `ProcessControlClient` owns process
startup, local control framing, timeouts and logs. It uses only inherited anonymous
stdin/stdout pipes, so no network/public control listener is exposed.

`vut_control_host` owns one Backend/SetupSession/PairingController on its owner
thread. It processes explicit setup commands and polls with a fresh post-poll
clock. Its I/O worker has bounded frames and one pending request; slow output
does not execute on the HID owner. EOF / close cancels setup and attempts cleanup.
No new HID packet, automatic vendor action or research recovery was introduced.

`vut_service --frontend-control` exposes status and explicit graceful stop through
the same versioned inherited-stream contract. Snapshots originate from the actual
ServiceRuntime; unsupported position/orientation fields stay Unknown. The I/O
worker has no Backend/runtime reference. Normal service shutdown does not depend
on frontend EOF: closing the window leaves a normal tracking service running.

The current inherited-stream frontend can control a tracking service it started.
After closing/reopening the GUI, an existing service is shown as external;
reattaching its control stream is unavailable. A same-session, exact-path service built with the v1 stop event can be stopped
gracefully from the reopened GUI, even if its status stream was lost. Older or
unverified instances must be stopped through their original launcher. Stop rechecks
executable/session identity and waits for exit; no arbitrary PID kill occurs.
Setup and tracking ownership are deliberately separate until live pairing/recovery
qualification is completed. Mutations are never automatically retried after an
unknown response. Cancel/timeout retires the uncertain stream and reports that state.

## States and diagnostics

Connection is observed event evidence. Tracking, stale receive age, stable identity,
pose validity and output are separate fields. At most five device rows are shown.
Metadata does not refresh pose age. Stable identity and body assignment are
Unavailable / Unsupported; battery is Unavailable. RF data is on Diagnostics /
Advanced, not represented as a stable device ID or body role.

Diagnostics copies application/OS version, owner mode, health, counts, pairing,
last message and HIL status. User-profile paths are redacted. Logs are under
`%LOCALAPPDATA%/MonakaVR/ViveUltimateTrackerBridge/logs`; they contain native stderr.
Internal exceptions belong in logs, while the normal screen gives actionable errors.

## Software validation

Native CMake/CTest covers the existing backend plus `frontend_control_stream` and
`setup_configuration`. These tests use fake transports, offline commands and
temporary config only. `--offline` never opens HID and supplies no fake tracker.

```powershell
./scripts/start_gui.ps1 -BuildOnly -Configuration Release
dotnet build ui/Control.Tests/Control.Tests.csproj -c Release --disable-build-servers -p:UseSharedCompilation=false -m:1
dotnet ui/Control.Tests/bin/Release/net8.0/Control.Tests.dll
dotnet build ui/Control.Smoke/Control.Smoke.csproj -c Release --disable-build-servers -p:UseSharedCompilation=false -m:1
dotnet ui/Control.Smoke/bin/Release/net8.0-windows/Control.Smoke.dll
```

Repeat with Debug. Smoke creates an offscreen five-device WPF rendering under
`build/ui-smoke`; it does not open a dongle or prove interactive/hardware behavior.
Fake implementations exist only in separate test/smoke projects, never the GUI's
normal client selection. Full native Debug/Release regression and software UI
checks are required before release. Actual pairing, device count, continuity,
ownership contention with real Hub/SteamVR and crash recovery are **HIL NOT RUN**.
