# Local Dynamic Analysis Test Harness

PowerShell-based test harness for reproducible local hardware-assisted dynamic analysis.

## Scope

Initial version automates repository precheck and fast-forward-only update, run ID and run directory creation, external logger/capture startup, target application startup, USB dongle presence monitoring, operator checkpoints, JSONL event timestamps, Git/device/environment snapshots, partial collection after failures or aborts, manifest/SUBMIT generation, and optional ZIP packaging.

The harness does not perform tracker power control, automatic pairing, Git reset/stash/commit/push, branch switching, USB reset, driver removal, or destructive source-tree cleanup.

## Branch

This first implementation lives on:

    experiment/vive-test-harness

It is derived from:

    experiment/vive-debug-console

## Important VIVE backend note

The current VIVE DebugConsole cannot open the dongle when the dongle is physically absent and then hotplug it later.

For that reason, the checked-in config uses:

    "startTiming": "afterDongle"

This gives the practical flow:

    PRECHECK (dongle absent)
    -> UPDATE
    -> START_LOGGING
    -> defer application
    -> WAIT_DONGLE
    -> start application
    -> operator pairing flow

If the target application supports hotplug, change the setting to:

    "startTiming": "beforeDongle"

which matches the nominal state-machine order exactly.

## First-time preparation

Build the DebugConsole before running the harness:

    powershell -ExecutionPolicy Bypass -File ..\scripts\run_vive_debug_console.ps1 -NoRun

Then switch to the harness branch and update:

    git fetch --no-prune origin
    git switch experiment/vive-test-harness
    git pull --ff-only

Because the executable is normally built outside tracked source files, it remains available after switching branches.

## Run

From the TestHarness directory:

    powershell -ExecutionPolicy Bypass -File .\run.ps1

Or specify another config:

    powershell -ExecutionPolicy Bypass -File .\run.ps1 -ConfigPath .\config.local.json

## Operator keys

At manual hardware checkpoints:

    Enter  completed
    F      failed
    S      skip
    Q      abort

An abort or failure still transitions through partial collection whenever possible.

## Git safety

Before update, the harness requires a clean git status, the exact configured branch, no residual configured application/logger process, and the target dongle physically absent.

Update is restricted to:

    git fetch <remote>
    git pull --ff-only <remote> <branch>

The harness never automatically performs reset, clean, stash, commit, push, checkout, or switch.

## Dongle detection

The default VIVE dongle filter is:

    USB\VID_0BB4&PID_0350*

Detection uses:

    Get-PnpDevice -PresentOnly

Polling defaults to 250 ms.

## External logger / USB capture

External loggers are configured in config.json under logging.processes.

Each entry supports name, kind, enabled, processName, executable, arguments, workingDirectory, stdoutFile, stderrFile, and stopMode.

Argument placeholders:

    {runDir}
    {runId}

The repository contains a disabled USBPcapCMD example. The USBPcap root/interface is machine-specific, so verify the -d value before enabling it.

The harness stops capture-kind processes first, waits flushWaitMs, then stops remaining loggers, then optionally stops the application.

CloseMainWindow is attempted when requested. If a started process remains alive, the harness may force-stop that process and records forcedStop=true in the manifest. It never kills residual processes discovered during PRECHECK.

## Run artifacts

Default location:

    TestHarness\runs\<RunId>\

Typical contents:

    manifest.json
    events.jsonl
    harness.log
    application.log
    application.err.log
    capture.pcapng
    capture.log
    capture.err.log
    git_status.txt
    environment.txt
    device_before.txt
    device_after.txt
    SUBMIT.md
    run.zip

Files that were not configured or not produced are omitted.

## Event timestamps

events.jsonl is one JSON object per line.

Minimum fields:

    timestamp
    event
    source
    state

Timestamps use ISO 8601 with milliseconds and local UTC offset.

Important event names include RunStarted, RepositoryUpdated, LoggingStarted, ApplicationStarted, DongleConnected, TrackerPairing1Completed, TrackerPowerOffCompleted, TrackerPairing2Completed, LoggingStopped, RunError, and OperatorAbort.

## Run result

manifest.json result values:

- completed
- failed
- aborted
- partial

A ZIP failure converts an otherwise completed run to partial but leaves the run directory intact.

## Current limitations

Initial version intentionally does not yet implement automatic pairing-state detection, automatic tracker-off detection, automatic reconnect detection, operator-action timeout, USBPcap interface auto-discovery, Ctrl+C injection for console capture tools, scenario-definition files, multi-tracker orchestration, or automatic success/failure classification.

These can be added without changing the run artifact model.
