# Task 3 dependency gate — MonakaProtocol C1

Task 3 consumes the fixed Task 1 protocol kit. It must not recreate the C1 schema/codec from this document or from memory.

## Verified Task 1 handoff

The following values were verified from the completed Task 1 implementation and the exact handoff already consumed successfully by Task 2:

- repository: `MonakaVR/MonakaProtocol`
- source commit: `5f41586b51bd84bb1cea344879b5d6325fc2c47d`
- schema commit: `04f2d6c831c68a65edfbfb66ff8838d1c9d78535`
- artifact: `monaka-protocol-kit-v1.0.zip`
- artifact SHA256: `eef5b7f2bc490926385b99dabcd44dc5a374228bf2a7869beea01f9dad936729`
- C1 SHA256: `3a76435c8b25b3975028f9a83c4dc3d1e3848806c9608d885f5bba74a1198ed3`
- `protocol.lock.json` SHA256: `234f0dffc46b808a179ec91da3c185794d7b7c83bc5b7d2bcb31fa73886bcb44`
- contract status: `candidate / master reconciliation pending`

The same kit is already present and verified in the Task 2 implementation branch of `orzkwsk/PicoMotionTrackerBridge`; that is evidence of the handoff values above, not permission for Task 3 to depend on a sibling live checkout.

## Required Task 3 import layout

When the exact artifact is supplied to this repository/workspace:

1. Verify the ZIP SHA256 before extraction.
2. Verify the external handoff manifest and the kit's internal `SHA256SUMS` / `protocol.lock.json`.
3. Extract the fixed kit under `third_party/monaka-protocol/`.
4. Store the source/kit/lock provenance under `dependencies/monaka-protocol.lock.json` (and retain the external handoff metadata needed to verify the artifact).
5. Link the supplied C++ codec as `MonakaProtocol::Codec`; do not copy/rewrite its public data model or JSON codec.
6. Use the kit's `docs/C1.md`, schemas and fixtures as the normative wire contract.

## Current repository preparation status

The GitHub preparation performed by the orchestrator does **not** contain the binary ZIP, because the available GitHub connector cannot transfer a private binary blob directly between repositories. No artifact hash has been fabricated and no alternate codec has been substituted.

Therefore:

- If Codex receives the exact ZIP/handoff in its Task 3 workspace, verify/import it and complete protocol-dependent implementation normally.
- If the ZIP is absent, perform only work that does not require fabricating the common contract: audit existing HID/backend behavior, add vendor-side regression tests (including `halfToFloat()`), prepare adapter/service structure, and document findings.
- Do **not** report C1 integration, end-to-end Observation encode/decode, or Task 3 Definition of Done as complete until the exact fixed kit is present and verified.

Do not fetch a floating/latest MonakaProtocol checkout and call it equivalent to this handoff. Any replacement artifact must match the fixed hashes above or be explicitly approved as a new synchronized protocol revision for all dependent tasks.
