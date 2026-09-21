# VIVE tracking-status HIL semantics

Status: HIL evidence summary, 2026-09-22.

Evidence file: `docs/hil/raw/2026-09-22-vive-tracking-status-pose-only.txt`.

## Observed status pairs

| Raw | Observed phase | Shared-boundary policy |
| --- | --- | --- |
| `0x02` | localized, moving-ish | `FULL` |
| `0x12` | localized, stationary-ish | `FULL` |
| `0x03` | not yet localized, moving-ish | `NONE` |
| `0x13` | not yet localized, stationary-ish | `NONE` |
| `0x04` | post-localization loss, moving-ish | `ROTATION_ONLY` |
| `0x14` | post-localization loss, stationary-ish | `ROTATION_ONLY` |

The moving/stationary wording is observational. The exact vendor meaning of bit `0x10` is not documented. HIL strongly correlates the three pairs `02/12`, `03/13`, and `04/14` with motion versus stationary/low-motion behavior, but code must continue to normalize only explicitly confirmed raw bytes rather than applying a general `raw & 0x0f` rule.

## Why 0x03 / 0x13 remain NONE

Before first localization, position remains zero while the vendor quaternion can respond to motion. On first localization, the transition `0x03 -> 0x02` can establish a materially different native tracking frame. One observed transition changed orientation by about 76.7 degrees while the first valid position appeared.

Therefore a numerically valid pre-localization quaternion is not sufficient evidence that the orientation is usable in the coordinate space later established by full tracking. Backend diagnostics may preserve it, but Common Observation must fail closed:

```
0x03 / 0x13
positionValid=false
orientation may be numerically present in backend
Common Observation modality=none
tracking_state=lost
```

## Why 0x04 / 0x14 are ROTATION_ONLY

After localization has already been established, optical loss transitions from `0x02` to `0x04`. Position freezes while quaternion updates remain continuous and respond to physical rotation. When the lost tracker becomes stationary, `0x04 -> 0x14` occurs without a pose-frame jump; small quaternion variation remains observable. Motion resumes the `0x04` state.

This supports preserving orientation in the already-established native tracking frame:

```
0x04 / 0x14
positionValid=false
orientationValid=true
Common Observation modality=rotation_only
tracking_state=degraded
orientation_evidence=device
```

## Implementation constraints

- Preserve `trackingStatusRaw` for diagnostics.
- Explicit mappings only: `0x12 -> Ok`, `0x13 -> Lost`, `0x14 -> RecentlyLost`.
- Do not generalize high-nibble masking.
- `0x03/0x13` and `0x04/0x14` are deliberately different at the Common Observation boundary.
- A non-finite or invalid-norm quaternion still fails closed to `NONE`.
- Future Codex work must treat this implementation as reviewable HIL-derived behavior, not undocumented vendor specification. If additional evidence is needed, request the specific HIL log or experiment instead of inferring semantics.
