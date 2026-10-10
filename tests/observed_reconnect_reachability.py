"""Static no-send boundary for inert research primitives; works with python -O."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "include/vut/research/observed_reconnect_wire.hpp"
RESEARCH_ALLOWLIST = {HEADER, ROOT / "tools/research/active_reconnect_session.hpp", ROOT / "tools/research/q1e_mapping_session.hpp"}
TOKENS = ("observed_reconnect_wire", "buildObservedBehavior2Report", "buildObservedQuery1eReport",
          "matchObserved1eResponseFrame", "kRfBehaviorObservedCode2", "kCmdObservedQuery1e",
          "ObservedFeatureReport", "Observed1eResponseFrame")
files = [p for base in ("src", "tools", "include", "scripts") for p in (ROOT / base).rglob("*")
         if p.suffix in (".cpp", ".hpp", ".h", ".py", ".ps1", ".cmake") and p not in RESEARCH_ALLOWLIST]
for path in files:
    source = path.read_text(encoding="utf-8")
    if any(token in source for token in TOKENS):
        raise RuntimeError("Research primitive reached production: " + path.relative_to(ROOT).as_posix())
source = HEADER.read_text(encoding="utf-8")
if re.search(r"Backend|Transport|setFeature|getFeature|sendCommand|HidD_|CreateFile", re.sub(r"//[^\n]*", "", source)):
    raise RuntimeError("Research primitive contains a runtime dependency")
protocol = (ROOT / "src/protocol.cpp").read_text(encoding="utf-8")
if "report[1] == 0x1e" not in protocol:
    raise RuntimeError("Existing optional report-ID recognition changed")
print("PASS production primitive call sites 0; no transport/executor dependency; existing 0x1e prefix recognition retained")
