"""Explicit checks remain active under python -O. No hardware access."""
from pathlib import Path
import re

root=Path(__file__).resolve().parents[1]
research=root/'tools/research'
def check(value,message):
    if not value: raise RuntimeError(message)
def code(path): return re.sub(r'//[^\n]*','',path.read_text(encoding='utf-8'))
session=code(research/'q1e_mapping_session.hpp');runner=code(research/'vut_q1e_mapping_hil.cpp')
for text in (session,runner):
    check(not re.search(r'buildObservedBehavior2|kRfBehaviorObservedCode2|active_reconnect_session|buildRfBehavior|buildFeatureCommand|beginPairing|sendAck|restartRf',text),'Non-Q1E sender reachable')
check(session.count('transport_.setFeature(')==1 and session.count('transport_.getFeature(')==1,'Multiple feature operations')
send=session.split('bool send(',1)[1].split('void sendReturnedAt',1)[0]
check('for (' not in send and 'while (' not in send,'Automatic retry loop')
check('research::buildObservedQuery1eReport()' in send,'Qualified builder bypassed')
check(send.index('++attempts_')<send.index('audit_(receipt)')<send.index('transport_.setFeature('),'Gate/audit not consumed before API')
check('Binding' not in session and 'candidateAt_' not in send and 'slot' not in send and '.mac' not in send,'Target selector required for targetless Q1E')
check('now-bOnAt_ >= 30000' in session and 'now-postAt_ < 30000' in session,'Thirty-second windows missing')
check('directory.parent_path()/"q1e-active-trial.lock"' in runner and 'DurableJournal trial(trialPath)' in runner,'One-trial journal missing')
check('arg == "--q1e-once"' not in runner,'Startup Q1E allowed')
for base in ('src','include','scripts','tools'):
    for path in (root/base).rglob('*'):
        if path.suffix not in ('.cpp','.hpp','.h','.py','.ps1') or research in path.parents: continue
        text=code(path)
        check('q1e_mapping_session' not in text and 'vut_q1e_mapping_hil' not in text,'Production exposure: '+path.name)
cmake=(root/'CMakeLists.txt').read_text()
check('option(VUT_BUILD_Q1E_MAPPING_HIL "Build isolated research-only Q1E mapping HIL runner" OFF)' in cmake,'Default build enabled')
for name in ('scripts/release_v2.py','scripts/release_v2.json'):
    text=(root/name).read_text();check('vut_q1e_mapping_hil' not in text and 'VUT_BUILD_Q1E_MAPPING_HIL' not in text,'Release exposure')
print('PASS Q1E-only/targetless/one-shot/no-retry/default-OFF; Behavior2/production/package reachability zero')
