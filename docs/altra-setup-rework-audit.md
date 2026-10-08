# Altra rework Phase 1 audit - 2026-10-09

Repository: `orzkwsk/ViveUltimateTrackerBridge`. Clean starting HEAD:
`68af2d05ad8ef7cfceff624cff66c58569edd853` on
`feature/altra-pairing-assignment-ux`. Original base
`e8ecd513391ab4f15af2941380631e7f05049bea` is an ancestor.
Rework branch: `fix/altra-pairing-assignment-ux-rework`; no history rewrite.
No concurrent working-tree change detected at entry. The external instruction
file is not copied into or committed to this repository.

## Read-only findings

| Boundary | Evidence and decision |
| --- | --- |
| Protocol | Fixed v2 source `572e58cfa20b8b4335207ea5dbcb3f04c587ddff`. C1 section 6(7) excludes roles from both wires; C2 preserves responsibility division and places body targets in MonakaVR. |
| Bridge | Branch `refactor/monaka-layer-separation`, observed commit `84f5b7529694d19aee98461580f3cfe628bf3cbc`. Config source/header map source/device to logical tracker, spatial profiles/transforms and routing. No body-role enum/apply operation in the inspected config/control contract. |
| Upper assignment | Cross-backend body assignment, resolver policy, IK and final solver output belong to MonakaVR. No API tying verified tracker identity to body target is available to this VUT path. Never infer roles from logical names, map IDs or connection order. |
| VUT at 68af2d0 | Local role vocabulary/uniqueness and legacy sidecar existed without upper contract. Choose instruction rule B: remove the role SoT; retain a caller-policy-based internal experiment outside production targets/launcher. |
| Pairing | Existing real begin/end and uncertain-response state; standalone bounded cleanup. Share the controller and retain one HID session across Pairing success and Identify. |
| Identity | RF-only production evidence; a stable-identity receiver but no verified per-tracker provider. Keep fail-closed; injection is not acquisition. |
| Exclusive HID | Stopped service/Hub remains necessary. Bring-up only; separate model/control/presentation for future service-owned control. |
| Robustness | Zero offline error exits, literal thresholds and crashed CREATE_NEW sentinel: replace with sticky failure, injectable provisional policy and OS lock ownership. |

## Sources and provenance

No related repository changes were made.

- [Protocol C2 at the fixed source](https://github.com/MonakaVR/MonakaProtocol/blob/572e58cfa20b8b4335207ea5dbcb3f04c587ddff/docs/C2.md).
- Bundled `third_party/monaka-protocol-v2/docs/C1.md` and `docs/C2.md`.
  ZIP/manifest/clean-source/internal sums/extraction checked read-only: PASS.
  ZIP SHA256: `a55567425d071e7338b89f4e6caa925676014fd356aa585730deb1f9d11cd7c3`.
- [Bridge config implementation](https://github.com/MonakaVR/MonakaBridge/blob/84f5b7529694d19aee98461580f3cfe628bf3cbc/src/mapping/config.cpp),
  blob `fed83a7c2ca6f2bec2df7cc9bd363b5e921b8fe8`.
- [Bridge config API](https://github.com/MonakaVR/MonakaBridge/blob/84f5b7529694d19aee98461580f3cfe628bf3cbc/include/monaka_bridge/config.hpp),
  blob `0d69cc8c564092e95d4c90a07fab911f177024c2`.
- Bridge branch architecture/configuration docs, README and example config.
  Historical pose/namespace descriptions in those docs do not supersede C2.

No wire schema/codec, body-role policy, map approval or coordinate transform is
changed. These findings justify no speculative related-repository modifications.
An authoritative upper assignment/apply API is needed before an adapter can exist.
