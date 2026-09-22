# Manual VIVE SLAM recovery controls

This diagnostic surface exists only to capture evidence when a tracker remains in
LOST/RECENTLY_LOST rotation-only tracking and does not relocalize. Normal
`vut_service` startup and automatic recovery do not send any of these commands.

Start the service from this repository with:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\start_vive_backend.ps1 `
  -NoBuild `
  -InteractiveDebug
```

`-InteractiveDebug` also enables normal detailed diagnostics so ACK/status
responses are visible in the same console. The launcher process guard treats
interactive-debug mode as a distinct service option.

The console auto-selects the first observed tracker slot. Press `0` through
`4` to explicitly select a slot, then use:

| Key | Command(s) | Purpose |
| --- | --- | --- |
| Q | `P63:0`, `P63:4`, `P63:5`, `P63:6` | Read transmission/map/tracking state |
| T | `ATM-1` then `ATM20` | Reassert single-tracker host tracking mode |
| E | `ALE` | End current map lifecycle step |
| M | `P64:1` | ASK_MAP |
| K | `P64:2` | keyframe sync request |
| Shift+X | `P64:3` | RESET_MAP; highest-risk control, use last |
| H | none | Print the key help again |

These are reverse-engineered vendor commands, not documented VIVE APIs. Do not
treat a sent command as evidence that the tracker accepted or acted on it; keep
`--diagnostics` output and tracking-status transitions as the evidence.

Recommended stuck-recovery experiment order is Q, then T, then E, then M, then
K. Use Shift+X only after the previous steps have been observed and recorded.
Do not re-pair merely to use these controls. A tracker sleep/wake or service
restart can independently change the observed state and should be recorded as a
separate intervention.

The implementation intentionally exposes a fixed allow-list instead of an
arbitrary raw ACK sender. It does not add any automatic SLAM recovery policy.
