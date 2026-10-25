"""Hardware-free Windows ownership harness. Only exact fixture handles are stopped."""
import argparse
import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import subprocess
import tempfile
import time

KERNEL = ctypes.WinDLL('kernel32', use_last_error=True)
KERNEL.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
KERNEL.OpenProcess.restype = wintypes.HANDLE
KERNEL.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
KERNEL.WaitForSingleObject.restype = wintypes.DWORD
KERNEL.TerminateProcess.argtypes = [wintypes.HANDLE, wintypes.UINT]
KERNEL.CloseHandle.argtypes = [wintypes.HANDLE]
KERNEL.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
KERNEL.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4

def require(condition, message):
    if not condition:
        raise RuntimeError(message)

def wait_for(predicate, message, seconds=8):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        value = predicate()
        if value:
            return value
        time.sleep(0.025)
    raise RuntimeError(message)

def read_json(path):
    try:
        return json.loads(path.read_text())
    except (FileNotFoundError, json.JSONDecodeError, PermissionError):
        return {}

class Case:
    def __init__(self, executable, root):
        self.exe, self.root = executable, root
        self.processes, self.child_handles, self.logs = [], [], []
        self.config, self.journal = root / 'service.conf', root / 'revision.log'
        self.config.write_text('fixture only; no HID backend linked\n')

    def launch(self, name, mode='normal', config=None, journal=None, port=29810, diagnostics=False):
        work = self.root / name
        work.mkdir()
        log = open(work / 'process.log', 'w')
        self.logs.append(log)
        args = [str(self.exe), '--agent', str(config or self.config), str(journal or self.journal), str(port), str(work), mode]
        if diagnostics:
            args.append('--diagnostics')
        process = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
        self.processes.append(process)
        return process, work

    def ready(self, process, work):
        wait_for(lambda: (work / 'ready').exists() or process.poll() is not None, 'fixture startup timeout')
        require(process.poll() is None and (work / 'ready').exists(), f'fixture failed: {work}')

    def child(self, work):
        wait_for(lambda: (work / 'child-started').exists() and (work / 'child-pid').exists(), 'child did not start')
        pid = int((work / 'child-pid').read_text())
        require(pid == int((work / 'child-started').read_text()), 'child identity receipt mismatch')
        handle = KERNEL.OpenProcess(0x00100000 | 0x1000 | 0x0001, False, pid)
        require(bool(handle), 'cannot hold owned fixture child handle')
        self.child_handles.append(handle)
        path = ctypes.create_unicode_buffer(32768)
        size = wintypes.DWORD(len(path))
        require(KERNEL.QueryFullProcessImageNameW(handle, 0, path, ctypes.byref(size)), 'cannot verify child executable')
        require(Path(path.value).resolve() == self.exe, 'unexpected child executable')
        return handle

    def stop(self, process, work):
        (work / 'stop').touch()
        require(process.wait(timeout=10) == 0, f'graceful fixture failure: {work}')
        data = read_json(work / 'result.json')
        require(data.get('shutdowns') == 1 and data.get('destroyed') == 1, 'shutdown/destruction must occur exactly once')
        require(data.get('ordering') is True, 'runtime shutdown must precede child/lock release')
        return data

    def cleanup(self):
        for process in self.processes:
            if process.poll() is None:
                process.terminate()  # Popen retains the exact process HANDLE.
                process.wait(timeout=10)
        for handle in self.child_handles:
            if KERNEL.WaitForSingleObject(handle, 0) == 258:
                KERNEL.TerminateProcess(handle, 97)  # Harness-created child only.
                require(KERNEL.WaitForSingleObject(handle, 5000) == 0, 'fixture cleanup failed')
            KERNEL.CloseHandle(handle)
        for log in self.logs:
            log.close()

def single(case):
    owner, work = case.launch('owner')
    case.ready(owner, work)
    original = case.journal.read_bytes()
    alias = case.config.parent / '.' / case.config.name.upper()
    second, second_work = case.launch('second', config=alias, diagnostics=True)
    require(second.wait(timeout=8) == 2, 'second instance accepted')
    result = read_json(second_work / 'result.json')
    require(result.get('factories') == result.get('starts') == result.get('mutations') == 0, 'second instance touched runtime/HID/journal')
    require(case.journal.read_bytes() == original, 'duplicate changed journal')
    case.stop(owner, work)

def graceful(case):
    owner, work = case.launch('owner', 'child')
    case.ready(owner, work)
    child = case.child(work)
    case.stop(owner, work)
    require(KERNEL.WaitForSingleObject(child, 2000) == 0, 'graceful stop left child residue')

def abnormal(case):
    owner, work = case.launch('owner', 'child')
    case.ready(owner, work)
    child = case.child(work)
    owner.terminate()
    owner.wait(timeout=8)
    require(KERNEL.WaitForSingleObject(child, 3000) == 0, 'abnormal owner death left child residue')

def child_crash(case):
    owner, work = case.launch('owner', 'child')
    case.ready(owner, work)
    child = case.child(work)
    before = wait_for(lambda: read_json(work / 'status.json').get('polls'), 'no heartbeat')
    (work / 'child-exit').touch()
    require(KERNEL.WaitForSingleObject(child, 3000) == 0, 'child did not exit')
    wait_for(lambda: (lambda x: x.get('child') == 2 and x.get('polls', 0) > before)(read_json(work / 'status.json')), 'tracking did not continue after child exit')
    require(owner.poll() is None, 'child crash stopped Agent')
    case.stop(owner, work)

def job_failure(case):
    owner, work = case.launch('owner', 'jobfail')
    case.ready(owner, work)
    require((work / 'job-rejected').exists(), 'job failure not reported')
    require(not (work / 'child-started').exists() and not (work / 'child-pid').exists(), 'job failure admitted unmanaged child')
    case.stop(owner, work)

def external(case):
    external_work = case.root / 'external'
    external_work.mkdir()
    process = subprocess.Popen([str(case.exe), '--child', str(external_work)], creationflags=subprocess.CREATE_NO_WINDOW)
    case.processes.append(process)
    wait_for(lambda: (external_work / 'child-started').exists(), 'external fixture did not start')
    owner, work = case.launch('owner', 'child')
    # Only a temporary negative-control fixture reads this. Product never does.
    created, exited, kernel, user = (wintypes.FILETIME() for _ in range(4))
    require(KERNEL.GetProcessTimes(int(process._handle), ctypes.byref(created), ctypes.byref(exited), ctypes.byref(kernel), ctypes.byref(user)), 'external fixture creation time unavailable')
    creation_time = (created.dwHighDateTime << 32) | created.dwLowDateTime
    (work / 'external.pid').write_text(f'{process.pid} {creation_time}')
    case.ready(owner, work)
    child = case.child(work)
    case.stop(owner, work)
    require(KERNEL.WaitForSingleObject(child, 2000) == 0, 'owned child remained')
    require(process.poll() is None, 'Agent killed unknown external process')

def release(case):
    first, work = case.launch('first')
    case.ready(first, work)
    case.stop(first, work)
    second, second_work = case.launch('second')
    case.ready(second, second_work)
    case.stop(second, second_work)

def distinct(case):
    first, work = case.launch('first')
    case.ready(first, work)
    other_config = case.root / 'other.conf'
    other_config.write_text('different software installation')
    # Isolate config identity: all other key fields remain exactly equal.
    second, second_work = case.launch('second', config=other_config)
    case.ready(second, second_work)
    case.stop(second, second_work)
    case.stop(first, work)
    print('P8 HIL_REQUIRED: different lock namespaces do not prove simultaneous HID access')

def unit_case(case):
    result = subprocess.run([str(case.exe), '--unit', str(case.root / 'unit')], capture_output=True, text=True, timeout=15)
    (case.root / 'unit.log').write_text(result.stdout + result.stderr)
    require(result.returncode == 0, 'lifecycle unit failure: ' + result.stderr)

CASES = {'unit': unit_case, 'P1': single, 'P2': graceful, 'P3': abnormal, 'P4': child_crash, 'P5': job_failure, 'P6': external, 'P7': release, 'P8': distinct}

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('exe', type=Path)
    parser.add_argument('--case', choices=CASES)
    parser.add_argument('--work', type=Path)
    args = parser.parse_args()
    if args.work:
        work = args.work
    else:
        artifacts = Path.cwd() / 'agent-process-artifacts'
        artifacts.mkdir(exist_ok=True)
        work = Path(tempfile.mkdtemp(prefix='run-', dir=artifacts))
    work.mkdir(parents=True, exist_ok=True)
    receipts = []
    try:
        for name in ([args.case] if args.case else [x for x in CASES if x != 'unit']):
            directory = work / name
            directory.mkdir(exist_ok=False)
            case = Case(args.exe.resolve(), directory)
            try:
                CASES[name](case)
                receipts.append({'case': name, 'status': 'PASS'})
                print('PASS', name, flush=True)
            except Exception as error:
                receipts.append({'case': name, 'status': 'FAIL', 'error': str(error)})
                raise
            finally:
                case.cleanup()
    finally:
        (work / 'receipts.json').write_text(json.dumps(receipts, indent=2))
        print('Evidence:', work, flush=True)

if __name__ == '__main__':
    main()
