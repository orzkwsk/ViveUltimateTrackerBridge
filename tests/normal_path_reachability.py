"""Offline production-source/header/build/package closure audit, also under -O."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]

def require(condition, message):
    if not condition:
        raise RuntimeError(message)

def code(text):
    return re.sub(r'//[^\n]*|/\*.*?\*/', '', text, flags=re.S)

def audit(root):
    root = Path(root)
    sources = sorted((root / 'src').glob('*.cpp')) + sorted((root / 'tools').glob('*.cpp'))
    headers = sorted((root / 'include/vut').rglob('*.hpp'))
    artifacts = ['observed_reconnect_wire', 'Q1E', 'Behavior2', 'RestartRf', 'USBPcap',
                 'active_reconnect', 'q1e_mapping', 'hil-private', 'vut/research']
    visited = set()
    def visit(path):
        if path in visited:
            return
        visited.add(path)
        body = code(path.read_text(encoding='utf-8-sig'))
        for artifact in artifacts:
            require(artifact.lower() not in body.lower(), f'research reachability: {path}: {artifact}')
        for header in re.findall(r'^\s*#\s*include\s*"([^"]+)"', body, re.M):
            if header.startswith('vut/'):
                dependency = root / 'include' / header
                require(dependency.is_file(), f'missing production include: {dependency}')
                visit(dependency)
    for path in sources + headers:
        visit(path)
    for name in ['CMakeLists.txt', 'scripts/release_v2.py', 'scripts/release_v2.json',
                 'scripts/start_vive_backend.ps1', 'scripts/setup_altra_trackers.ps1']:
        text = (root / name).read_text(encoding='utf-8-sig')
        text = re.sub(r'(?m)^\s*#[^\n]*', '', text)
        for artifact in artifacts:
            require(artifact.lower() not in text.lower(), f'research build/package reachability: {name}: {artifact}')
    service = code((root / 'tools/vut_service.cpp').read_text(encoding='utf-8-sig'))
    require(service.count('runtime.start()') == 1, 'service must have exactly one explicit startup')
    require(service.count('while(run)') == 1, 'service must only poll, not automatically reopen')
    runtime = code((root / 'src/service_runtime.cpp').read_text(encoding='utf-8-sig'))
    dispatch = runtime.split('void ServiceRuntime::dispatch', 1)[1].split('void ServiceRuntime::rememberStatistics', 1)[0]
    require('backend_.' not in dispatch, 'dispatcher cannot call Backend')
    require(dispatch.index('epochs_.observe(e)') < dispatch.index('handle = receive(e)')
            < dispatch.index('epochs_.retainBindings') < dispatch.index('session_->adapter->admit(e)'),
            'epoch/registry/ownership/admission ordering')
    print(f'PASS production closure ({len(visited)} C++ files), build/package/launcher; research recovery references=0')

if __name__ == '__main__':
    try:
        audit(ROOT)
    except Exception as error:
        print(error, file=sys.stderr)
        raise SystemExit(1)
