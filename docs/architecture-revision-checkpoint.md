# Architecture revision software checkpoint

Wire v2 publisher uses the pinned C2 kit (source 572e58cfa20b8b4335207ea5dbcb3f04c587ddff, ZIP a55567425d071e7338b89f4e6caa925676014fd356aa585730deb1f9d11cd7c3). Status OK with paired valid pose becomes FULL; Lost/RecentlyLost are NONE. Numeric quaternion sanity does not establish independently usable rotation. No fake IMU identity.

The existing half-range RF counter and >500ms gap/re-pair fail-closed restart policy remains conservative: RF continuity/map-origin semantics have no hardware evidence permitting safe relaxation. Automatic recovery of that native gap remains unresolved, not a claimed PASS.

Validation 2026-09-16: MSVC Release build PASS; CTest 7/7 PASS, including 33 fixed v2 fixtures, 22 cross-language directions, seven synthetic vendor status samples and actual loopback UDP. Hardware VIVE/rotation-only/Hub bypass/axis/occlusion NOT RUN. No cutover claim.
