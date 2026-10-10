"""Explicit research-boundary / no-retry audit, including under python -O."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
RESEARCH = ROOT / "tools/research"
def check(value, message):
    if not value:
        raise RuntimeError(message)
def source(path):
    return re.sub(r"//[^\n]*", "", path.read_text(encoding="utf-8"))
for base in ("src", "include", "scripts", "tools"):
    for path in (ROOT / base).rglob("*"):
        if path.suffix not in (".cpp", ".h", ".hpp", ".py", ".ps1") or RESEARCH in path.parents:
            continue
        check("active_reconnect_session" not in source(path) and "vut_active_reconnect_hil" not in source(path),
              "Research runner reachable from production: " + path.relative_to(ROOT).as_posix())
cmake = (ROOT / "CMakeLists.txt").read_text()
check('option(VUT_BUILD_ACTIVE_RECONNECT_HIL "Build isolated research-only single-B active HIL runner" OFF)' in cmake,
      "Research runner must default OFF")
check('VUT_BUILD_ACTIVE_RECONNECT_HIL' not in (ROOT / 'scripts/release_v2.py').read_text(), "Release enables HIL")
check('vut_active_reconnect_hil' not in (ROOT / 'scripts/release_v2.py').read_text(), "Release packages HIL")
session = source(RESEARCH / "active_reconnect_session.hpp")
windows = source(RESEARCH / "active_reconnect_windows.hpp")
runner = source(RESEARCH / "vut_active_reconnect_hil.cpp")
check(session.count("transport_.setFeature(") == 1 and session.count("transport_.getFeature(") == 1, "Unbounded feature call sites")
send = session.split("bool send(", 1)[1].split("void transportLost", 1)[0]
check("for (" not in send and "while (" not in send, "Send path contains a retry loop")
check(windows.count("HidD_SetFeature(") == 1 and windows.count("HidD_GetFeature(") == 1, "Unexpected HID send/query call site")
for code in (session, windows, runner):
    check(not re.search(r"beginPairing|endPairing|sendAck|buildAckTx|restartRf|endPairingWithRetry|Backend\s+\w+", code), "Forbidden production/exchange path")
check("++attempts_[i]" in send and send.index("++attempts_[i]") < send.index("transport_.setFeature("), "Gate consumed too late")
check(send.index("audit_(receipt)") < send.index("transport_.setFeature("), "Missing durable pre-send receipt")
check("CREATE_NEW" in windows and '"trial.lock"' in runner and '"owner.lock"' in runner, "Missing persistent one-trial/process lock")
check('else if (arg == "--run-dir"' in runner and 'arg == "--send' not in runner, "Startup send option")
print("PASS production reachability 0; default build/package excluded; one-call/no-retry/durable-intent boundaries")
