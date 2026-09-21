# Architecture revision software checkpoint

Wire v2 publisher uses the pinned C2 kit (source 572e58cfa20b8b4335207ea5dbcb3f04c587ddff, ZIP a55567425d071e7338b89f4e6caa925676014fd356aa585730deb1f9d11cd7c3). Status OK with paired valid pose becomes FULL; Lost/RecentlyLost are NONE. Numeric quaternion sanity does not establish independently usable rotation. No fake IMU identity.

The half-range RF counter and >500ms ambiguous-gap policy remains fail-closed.
Re-pair also remains revision-advancing by default. The later experimental
`reconnect_revision preserve` opt-in may retain revision only for an exact
configured device reconnect in one service/config lifetime; it does not relax
ambiguous RF gaps or automatically rebind an address. Its only physical basis is
the limited 2026-09-21 observation documented in the production guide, not a
vendor map-continuity guarantee.

Validation 2026-09-16: MSVC Release build PASS; CTest 7/7 PASS, including 33 fixed v2 fixtures, 22 cross-language directions, seven synthetic vendor status samples and actual loopback UDP. Hardware VIVE/rotation-only/Hub bypass/axis/occlusion NOT RUN. No cutover claim.
