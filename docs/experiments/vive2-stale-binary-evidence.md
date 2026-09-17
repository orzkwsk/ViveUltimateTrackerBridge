# `vive2.pcapng` stale-binary evidence

Date: 2026-09-18 JST

Capture under review: `vive2.pcapng` (external HIL artifact; not committed here).

This capture does **not** validate the slot-specific runtime RestartRf experiment.

Observed sequence:

```text
22:57:56.515652  cold-start RestartRf
                  02 01 00 01 01 01 01

22:58:05.903727  pair event slot0
                  23:30:03:81:fe:ec

22:58:15.367978  unpair event slot0
                  23:30:03:81:fe:ec

22:58:15.368024  runtime RestartRf
                  02 01 00 01 01 01 01
```

Expected runtime payload for the current slot0-specific source is:

```text
02 01 01 00 00 00 00
```

Therefore the capture was produced by a stale/incorrect `vut_probe` binary or checkout. It must not be used to judge `F-HIL-RECONNECT-02-slot-specific` PASS/FAIL.

The capture ends immediately after the runtime RestartRf, so there is also no valid post-command reconnect observation window in this artifact.

Next HIL must positively identify the executable before testing. A unique probe build marker and executable SHA-256 should be recorded before capture starts.
