# VIVE Ultimate Tracker runtime reconnect debug — Codex handoff

Updated: 2026-09-17

This document is for Codex while hardware debugging continues in parallel.

## Repository / branches

Repository:

```text
orzkwsk/ViveUltimateTrackerBridge
```

Normal coordinated implementation baseline:

```text
branch: refactor/monaka-layer-separation
HEAD at handoff creation: fc5cd080392b77056675d85459f735f0385af543
```

Hardware experiment branch:

```text
branch: experiment/vive-auto-reconnect
baseline: fc5cd080392b77056675d85459f735f0385af543
experimental HEAD before this document: 543a3f0d416d11993d4d8c8c5d7a2aeacbdd1157
```

The experiment branch is a direct descendant of the coordinated refactor baseline. Treat it as an evidence branch, not as an automatically mergeable implementation branch.

## Required Codex operating mode

Codex work for the coordinated Monaka layer-separation task must remain based on `refactor/monaka-layer-separation` (or a Codex-managed writable checkout with verified ancestry from it).

Before doing anything:

```text
git fetch --no-prune origin
```

Then verify:

```text
origin/refactor/monaka-layer-separation
origin/experiment/vive-auto-reconnect
```

Read completely:

```text
AGENTS.md
docs/codex/Task3_VIVE_Backend.md
docs/codex/task3/00_task1_handoff_status.md
```

For reconnect evidence, inspect the experiment branch read-only, including:

```text
docs/experiments/vive-auto-reconnect-hil.md
docs/experiments/vive-auto-reconnect-slot-specific-hil.md
```

If this handoff file is being read from the normal branch rather than the experiment branch, obtain it with `git show origin/experiment/vive-auto-reconnect:docs/codex/VIVE_RuntimeReconnect_Debug_Handoff.md`.

## Critical separation rule

Do **not** merge, cherry-pick, squash, or otherwise transfer the reconnect experiment into `refactor/monaka-layer-separation` merely because code exists on the experiment branch.

Hardware validation is still in progress.

Do not rewrite the experiment history. In particular do not rebase/reset/amend away failed experiments; they are evidence.

## Experiment history and current evidence

### 1. Cold-start RestartRf

Commit:

```text
8df0f91bb82141b7ad97de3ad1c108e0fbe19080
Experiment with Hub-style RestartRf on backend startup
```

Cold-start payload currently under test:

```text
02 01 00 01 01 01 01
```

Observed result: already-paired tracker can reconnect after backend/dongle cold start. This has hardware evidence, but do not generalize the unknown trailing bytes into a documented vendor format.

### 2. Reusing cold-start RestartRf after runtime unpair

Commit:

```text
08290bb67dfbc6eaf6559178c8bad89cb9a5640d
Re-arm RestartRf after tracker unpair
```

Result in a clean Hub-bypass run: FAIL.

Observed slot0 sequence included:

```text
connected RF state: 0x32000008
runtime unpair
RestartRf: 02 01 00 01 01 01 01
post-command RF state: 0x320FA008
```

The RF state remained at the same raw value for tens of seconds and no second pair event appeared. Normal tracker restart and manual re-pair were unsuccessful in that run.

Keep this failed commit in history.

### 3. Passive RF-status diagnostics

Commit:

```text
fbc4f8e319681f8f1a7c8c80395ec69ddffd29f7
Add passive RF status diagnostics for reconnect HIL
```

Adds raw parsing/display of `0x1D` / `0x1E` RF status reports.

The five 32-bit slot states are diagnostic values only. Do not promote observed values such as the following into stable enums or shared MonakaProtocol semantics:

```text
0x32000008
0x0A000005
0x320FA008
0x04000003
```

The diagnostic parser/tooling may become a candidate for normal-branch integration after review because it does not itself define reconnect policy, but Codex must not transfer it automatically while hardware work is active.

### 4. Contaminated reconnect success

A run successfully reconnected, but the USB capture contained additional VIVE/HTC control traffic including slot-specific RestartRf and a second Hub-like initialization sequence.

That run proves the dongle/tracker can reconnect, but does **not** prove the standalone Backend did so.

Treat it as contaminated evidence, not Backend PASS.

### 5. Slot-specific runtime RestartRf experiment

Implementation commit:

```text
1c8f8a599dc92062bf44e3b79503f921d64636dd
Use slot-specific RestartRf after runtime unpair
```

Procedure commit / branch state:

```text
543a3f0d416d11993d4d8c8c5d7a2aeacbdd1157
Add slot-specific RestartRf reconnect HIL procedure
```

Observed Hub payloads:

```text
slot0 -> 02 01 01 00 00 00 00
slot2 -> 02 01 00 00 01 00 00
```

Values for slots 1/3/4 are currently inferred from the observed one-hot-like pattern, not independently captured.

Do not describe the trailing five bytes as a formally confirmed slot mask.

The slot-specific experiment is not approved for normal-branch integration until clean, repeated HIL establishes its behavior.

## Identity / status guardrails

- The six-byte RF address currently called `mac` contains slot/session addressing information and is not proven to be persistent hardware identity.
- `slotFromMac()` using the low nibble of byte 1 is vendor addressing behavior, not stable identity.
- ACK fields such as `NADS...` are stable-identity candidates only; they are not yet formally promoted to persistent device ID.
- pose raw tracking status `3` has strong evidence for position-invalid / rotation-continuing behavior; status `4` likely does too, but do not change shared protocol semantics solely from this debug work.
- `positionValid` / `orientationValid` are backend-derived from raw status, not independent vendor validity flags.
- raw acceleration/angular velocity semantics remain unverified.

## What Codex MAY do now

While hardware validation is ongoing, Codex may perform work that does not change RF behavior:

1. Audit the experiment branch against `refactor/monaka-layer-separation`.
2. Produce a commit-by-commit classification:
   - diagnostic-only candidate
   - behavior change requiring HIL
   - documentation/evidence only
   - must not integrate yet
3. Review RF-status parser bounds checking and tests.
4. Review whether reconnect diagnostics remain strictly inside the vendor backend boundary and do not leak into MonakaProtocol shared semantics.
5. Prepare tests or refactoring that preserve exact behavior and can be validated without hardware, but only on a separate Codex work branch/checkpoint derived from the normal coordinated branch.
6. Prepare a proposed integration plan for after the user declares a reconnect experiment PASS.
7. Run build/unit/mock tests that do not claim hardware results.

## What Codex MUST NOT do yet

- Do not merge/cherry-pick `experiment/vive-auto-reconnect` into `refactor/monaka-layer-separation`.
- Do not declare runtime reconnect PASS.
- Do not replace the cold-start payload based on assumptions.
- Do not add PairDevice or clear-pairing commands as speculative recovery.
- Do not call unknown RestartRf bytes a confirmed slot mask.
- Do not convert RF status raw words into stable enums without independent evidence.
- Do not promote the six-byte RF/session address to persistent identity.
- Do not alter MonakaProtocol to represent these vendor debug fields.
- Do not add VIVE Hub automation or depend on VIVE Hub being present.
- Do not erase failed experiment commits.
- Do not claim any HIL result that Codex itself did not execute and observe.

## Recommended Codex task

Perform a read-only-to-code-review pass over the reconnect experiment while preserving the coordinated branch.

Expected deliverable:

```text
normal_base_branch:
normal_base_sha:
experiment_branch:
experiment_head_sha:
ancestry_verified:

experiment_commits:
  - sha
  - purpose
  - observed evidence
  - integration classification
  - risks

diagnostic_parser_review:
unit_test_gaps:
normal_branch_changes_made:
build/test commands and results:
hardware_status: NOT RUN unless actually run
recommended post-HIL integration sequence:
```

If you identify a correctness bug in diagnostic-only code that is independent of reconnect behavior, you may prepare a narrow forward commit on your Codex work branch, but do not merge it into the hardware experiment branch and do not alter RF behavior.

## Integration gate

Reconnect behavior may move toward the normal coordinated branch only after the user/hardware test records enough clean evidence to distinguish:

```text
VIVE/HTC processes absent
fresh dongle state
known runtime RestartRf payload actually observed
pair event reappears or does not reappear
pose stream resumes or does not resume
RF status transition recorded
```

Prefer repeated clean success before calling the behavior stable.

Once the user explicitly marks a specific experiment revision as PASS, re-audit only the required commits and integrate the smallest proven subset into a fresh forward commit based on the current `refactor/monaka-layer-separation` HEAD. Do not blindly merge the whole experiment branch.
