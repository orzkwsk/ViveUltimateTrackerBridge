# Architecture revision software checkpoint

Wire v2 publisher uses the pinned C2 kit (source 572e58cfa20b8b4335207ea5dbcb3f04c587ddff, ZIP a55567425d071e7338b89f4e6caa925676014fd356aa585730deb1f9d11cd7c3). HIL on 2026-09-22 refined the VIVE mapping: raw 0x02/0x12 become FULL; pre-localization 0x03/0x13 remain NONE; post-localization loss 0x04/0x14 becomes ROTATION_ONLY when the quaternion passes numeric validation. Raw status is preserved and no general high-nibble mask is assumed. Evidence and rationale are in docs/hil/vive-tracking-status-semantics.md. No fake IMU identity.

The half-range RF counter and >500ms ambiguous-gap policy remains fail-closed.
Re-pair also remains revision-advancing by default. The later experimental
`reconnect_revision preserve` opt-in may retain revision only for an exact
configured device reconnect in one service/config lifetime; it does not relax
ambiguous RF gaps or automatically rebind an address. Its only physical basis is
the limited 2026-09-21 observation documented in the production guide, not a
vendor map-continuity guarantee.

Validation 2026-09-16: MSVC Release build PASS; CTest 7/7 PASS, including 33 fixed v2 fixtures, 22 cross-language directions, seven synthetic vendor status samples and actual loopback UDP. Subsequent 2026-09-22 HIL established the raw-status distinction documented under docs/hil; the new ROTATION_ONLY shared-boundary implementation still requires a fresh local build/CTest and end-to-end HIL after commit 94e5444bb0e4dcdecfb757742ab14af1b4274500. No cutover claim.
