# Initial research: VIVE Hub bypass backend

## Scope

This phase uses the official VIVE Ultimate Tracker USB dongle and bypasses the VIVE Hub/Streaming Hub process. Wi-Fi-only transport and tracker-count-limit bypass are explicitly deferred.

## Findings used for the implementation

Public reverse-engineering work shows two useful paths:

1. direct tracker USB HID; and
2. official dongle USB HID carrying RF traffic for up to the normal dongle topology.

For the current project the dongle path is the useful one. The observed USB IDs are HTC/VIVE VID `0x0bb4`, dongle PID `0x0350`, tracker PID `0x06a3`.

The dongle emits tracker-incoming reports with a compact envelope containing packet index, a six-byte device address, payload type and payload length. Payload type `0x0110` carries tracker pose data and `0x0101` carries tracker ACK/status text.

Observed pose payload sizes are 0x25 or 0x27 bytes. The 0x25-byte base layout is:

- byte 0: tracker packet/index field;
- byte 1: button fragment;
- bytes 2..13: position as three little-endian float32 values;
- bytes 14..21: quaternion as four float16 values emitted in `[w,z,y,x]` order;
- bytes 22..27: three float16 acceleration fields;
- bytes 28..35: four float16 rotation-velocity fields;
- byte 36: tracking status.

Tracking status behavior observed by the public implementations is important for the normalized backend contract:

- 2: position + orientation valid;
- 3: orientation continues while position is unavailable;
- 4: position is frozen/lost while orientation continues.

Therefore `positionValid` must only be true for status 2. Status 3/4 must not be consumed as an absolute 6DoF sample even though numeric position fields are present.

The public RE also shows the tracker topology being configured through text ACK commands sent through the dongle. The first active tracker is configured as SLAM host (`ATM20`) and later trackers as SLAM clients (`ATM11`). This is retained as a minimal bring-up sequence because the goal is to remove the Hub process, not to replace the tracker-side SLAM topology in this phase.

## Architecture decision

The implementation follows the same boundary used by `PicoMotionTrackerBridge`:

```text
VIVE Ultimate Tracker(s)
        ↓ proprietary RF
official VIVE USB dongle
        ↓ HID
VUT device backend
  enumeration / control / packet decoding / validity
        ↓
Normalized TrackerState
================================ backend boundary
        ↓
future MonakaVR / fusion / SteamVR output
```

Coordinate conversion, mount transforms, IK and SteamVR policy are intentionally excluded from the backend.

## Deliberate limitations of this first implementation

- Windows HID transport only.
- Official dongle only; no Wi-Fi-only bridge.
- No attempt to exceed the normal dongle topology.
- Pair/re-pair and base pose streaming are implemented; map-transfer recovery logic is not yet complete.
- Battery extraction is not yet normalized because the reliable source/units still need hardware verification.
- The acceleration and four-component rotation-velocity fields are exposed as raw decoded values until their frames/semantics are confirmed on hardware.
- The pose position is normalized as meters based on the current public implementations; this needs a physical-distance sanity check on the target trackers.

## Reference projects

- `shinyquagsire23/vive_ultimate_tracker_re` — original public protocol reverse engineering.
- `zengguizhang/pyvut` / `nijkah/pyvut` — later 6DoF/dongle work and pose API.
- local `PicoMotionTrackerBridge` — architectural reference only.

No third-party source code is copied into this repository. Protocol facts and observed packet formats were reimplemented independently in C++.
