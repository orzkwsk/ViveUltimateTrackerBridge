# USBPcap capture infrastructure qualification

**Classification: CAPTURE_INFRASTRUCTURE_QUALIFIED.** Test A, B and C passed
the saved-file, target-traffic and application-lifecycle checks using the installed
Wireshark GUI/extcap path. `magic = 0x00000000` was not reproduced in these captures.
Manual capture timing has the limitation recorded below.

Branch: `research/usbpcap-capture-infrastructure-qualification`.
Exact base: `f3f6563042a021d35ecb141c29eb017efe5c4c71`.
Execution date: 2026-10-10 JST. Prepared-lineage ancestry verified.
Source of Truth: `MonakaVR_USBPcap_Capture_Infrastructure_Qualification_Codex指示書.md`,
SHA256 `98fac76540403219f29bfd010625d060923f078ceae274d3c3e2c1de2da11647`.
Historical launcher/health results were context only; their captures were not reused.

## Installed environment and selection

| Item | Observed value |
|---|---|
| Windows | Windows 11 Home, 64-bit, 10.0.26200 / build 26200 |
| Wireshark | 4.6.6 |
| TShark / dumpcap | 4.6.6, v4.6.6-0-g3a22c3ef473d |
| USBPcapCMD / installed extcap | 1.5.4.0 |
| USBPcapCMD SHA256 | `8b13173e9453fda9914d707c732880de44081e20e1113b3a2c827abd7a28b15d` |
| USBPcap driver | Running / OK initially; Running / OK at final inventory |
| Current extcap interfaces | Five; target root uniquely correlated |
| Public target aliases | DONGLE-BUS / DONGLE, VID/PID `0bb4:0350` |

Present-only PnP found one healthy target. Its parent matched one USBPcap root
through zero-access, read-only USBPcap hub-symlink metadata. This opened the
USBPcap metadata control, not dongle HID. The installed extcap configuration
exposed one enabled direct USB device on that root. The current extcap device
value was used; the PnP port number was not substituted as a capture address.
The present target and root were rechecked before B.

Actual installed option names were read from `--extcap-config`:
`--snaplen`, `--bufferlen`, `--capture-from-all-devices`,
`--capture-from-new-devices`, `--inject-descriptors`, `--devices`.
Snapshot/buffer defaults were retained. Descriptor injection was enabled in all
three captures. A enabled all/new-device capture with no individual device
selection or device-specific capture filter. B disabled all/new-device capture
and selected only the current target; C retained B's selection.

## Qualification matrix

| Check | Test A | Test B | Test C |
|---|---|---|---|
| Application session | Fresh; no prior Wireshark/capture actor | Fresh after complete A exit | Same process and creation time as B |
| Administrative launch | Human confirmed | Human confirmed | Same B session |
| Mode | Root / all devices | Current target only | Current target only, repeat |
| New private saved file | Yes | Yes | Yes |
| Header | PCAPNG, `0a0d0d0a` | PCAPNG, `0a0d0d0a` | PCAPNG, `0a0d0d0a` |
| Offline `tshark -r` parse | PASS, exit 0 | PASS, exit 0 | PASS, exit 0 |
| Packet count category | 10-99, greater than zero | 10-99, greater than zero | 10-99, greater than zero |
| Unique target descriptor / current tuple | Confirmed | Confirmed | Confirmed |
| Same-tuple non-injected live traffic | Control and interrupt | Control and interrupt | Control and interrupt |
| Normal Stop | Human confirmed | Human confirmed | Human confirmed |
| Capture actors after Stop | Zero | Zero | Zero |
| Result | **A_PASS** | **B_PASS** | **C_PASS** |

Each file had exactly one target descriptor tuple matching the current root/device
inventory. Nonzero-IRP target control/interrupt records were checked separately
from injected descriptors. Injected descriptors alone did not establish PASS.
Interrupt records did not contain nonempty report data in these captures;
this result establishes capture traffic availability, not RF, pose or tracker connection.

The human performed administrative launch, configuration, capture, Stop and save
through the normal GUI. Initial agent computer-use approval timed out; a subsequent
screen read succeeded, but injected inputs did not visibly open the requested
controls. The cause was not established. Source of Truth section 18's human
handoff was used. The computer-use session was reset after the human prohibited
idle control ownership; subsequent steps did not retain or reconnect that session.

The requested capture window was 5-10 seconds, and the human confirmed that
window for each normal Stop. Actual first-to-last packet spans were **12.146 s,
15.963 s and 16.327 s** for A/B/C. These spans already exceed 10 seconds;
the exact 5-10-second duration requirement was not demonstrated. Start/Stop
instants were not independently instrumented. The PASS matrix qualifies saved
file/traffic and fresh/reused GUI lifecycle behavior; it does not qualify an
exact capture-duration bound. No additional capture was added to correct timing.

## Evidence and completion

| Private capture alias | SHA256 |
|---|---|
| A: root-all | `fddff24f1e25ed322ab8f5fb231ecdd1f58bd763ba0158fbd3662cc6814c67cb` |
| B: target-only | `2df0605d30404a9d5f9d366dc32ea91e703ce931f4a8129cd889ef47d7e28f5e` |
| C: target-only-repeat | `fd652edbb80ca8f885379a7f3180831320e44c823bd4ab527e2d2b9f0a25e122` |

Raw captures, offline CSV, first bytes, GUI image, actual topology/device address,
PnP/identity values, process inventory, exact configuration and operator receipts
remain private and gitignored. This report is the only public change.

Complete application exit was verified before A, between A and B, and after C.
Final Wireshark / dumpcap / USBPcapCMD / TShark / Bridge actor count was zero.
No forced process cleanup was used. SteamVR's four inventoried processes and
VD Service/Streamer retained their initial instances. VD Server's instance changed
between inventory samples; the agent performed no VD lifecycle operation, and
no cause or ownership conclusion is inferred. Existing tracker runtime state
was inventoried read-only; it was not started or stopped by the agent.

Physical preflight was human-confirmed for the whole qualification: A/B OFF,
both charging USB disconnected, dongle connected. No tracker ON or pairing was
requested. Bridge/HIL execution, dongle HID open, SetFeature/GetFeature and agent
vendor sends were **NOT RUN / zero**. No install, driver/service/registry change,
replug or reboot occurred. VRChat/SteamVR/VD lifecycle operations by the agent: zero.

Validation: all three real saved captures passed offline parse, header, packet,
descriptor and non-injected traffic checks. Sanitized-report leakage scan:
PASS against 113 actual private identifier representations. Capture/report hash
cross-check: PASS. `git diff --check`: PASS before commit. Production build/full CTest:
NOT RUN, because this change contains only a report. Production changes and
integration/HIL branch merges: none.

This qualifies the tested GUI/extcap matrix only. TShark live orchestration was
not tested and its historical failure is not declared fixed. Altra protocol,
Q1E/Behavior2 semantics, identity acquisition and hardware safety conclusions
are unchanged. Q1E HIL was not resumed; any future fresh HIL belongs to a separate task.
