# Verification lanes — input producer

The backend supplies actual tracker observations and session/transport state. The consumer owns body constraints and the user owns optional spatial recalibration; routine reconnects should not be treated as product-quality acceptance ceremonies.

- `powershell -File scripts/test_revision.ps1 -Mode Quick`: build and selected tests covering registry, real backend code with synthetic inputs, pose observation, service behavior, pairing and loopback transport.
- `powershell -File scripts/test_revision.ps1 -Mode Full`: broad native functional CTest suite, excluding frozen protocol-kit checks and the old strict session-revision-policy test.
- `powershell -File scripts/test_revision.ps1 -Mode Audit`: historical complete CTest, protocol/release tool integrity and session-policy compatibility.

No software lane establishes live dongle/HID pairing, SLAM map reuse or correctness, nor the Windows host/user workflow. Physical HIL is separate and must not be inferred from software PASS.

The current production default `reconnect_revision=strict` remains unchanged by this script-only patch. Coordinated runtime policy remediation belongs under MonakaBridge#17, with explicit synthetic reconnect/pose-continuity tests. Keep true packet/identity/validity/age checks; do not require physical map-certification proof before publishing vendor-valid observations.
