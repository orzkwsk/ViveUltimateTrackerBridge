# Local Dynamic Analysis Test Harness

PowerShell operator-assisted HIL harness.

Run after building DebugConsole:
  powershell -ExecutionPolicy Bypass -File ..\scripts\run_vive_debug_console.ps1 -NoRun
  powershell -ExecutionPolicy Bypass -File .\run.ps1

State machine: INIT -> PRECHECK -> UPDATE -> START_LOGGING -> START_APPLICATION -> WAIT_DONGLE -> WAIT_PAIR_1 -> WAIT_POWER_OFF -> WAIT_PAIR_2 -> STOP_LOGGING -> COLLECT -> PACKAGE -> DONE.
Errors go to ERROR -> COLLECT_PARTIAL -> DONE.

Safety: no stash/reset/clean/commit/push/branch-switch/device-reset. Dirty tree or branch mismatch stops the run.

Output: runs/<RunId>_<shortCommit>/ with manifest.json, events.jsonl, harness.log, app logs, Git/PnP snapshots, SUBMIT.md and optional run.zip.

Default dongle pattern: USB\VID_0BB4&PID_0350*.
External logger processes can be added under logging.processes; v1 does not hard-code USBPcap CLI.
