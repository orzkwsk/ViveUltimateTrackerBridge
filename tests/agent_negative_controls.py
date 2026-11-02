"""Build disposable mutants; require the actual ownership harness to reject each.

Only the temporary fixture may terminate the harness-created external process.
No discovery, real HID, product runtime switch or product-source mutation.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

def replace_once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('mutation anchor changed: ' + old)
    return text.replace(old, new, 1)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--cmake', required=True)
    parser.add_argument('--config', choices=['Debug', 'Release'], default='Release')
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=False)
    source = args.work / 'source'
    source.mkdir()
    (source / 'include/vut').mkdir(parents=True)
    (source / 'include/vut/agent_lifecycle.hpp').write_bytes((ROOT / 'include/vut/agent_lifecycle.hpp').read_bytes())
    native = (ROOT / 'src/agent_lifecycle.cpp').read_text()
    fixture = (ROOT / 'tests/agent_lifecycle_tests.cpp').read_text()
    external_mutation = '''++counts_.shutdowns;
        std::ifstream victim(counts_.work / "external.pid");
        DWORD pid = 0;
        unsigned long long expectedCreated = 0;
        if (victim >> pid >> expectedCreated) {
            HANDLE handle = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (handle) {
                FILETIME created{}, exited{}, kernel{}, user{};
                if (GetProcessTimes(handle, &created, &exited, &kernel, &user) &&
                    ((static_cast<unsigned long long>(created.dwHighDateTime) << 32) | created.dwLowDateTime) == expectedCreated) {
                    TerminateProcess(handle, 96); WaitForSingleObject(handle, 5000);
                }
                CloseHandle(handle);
            }
        }'''
    mutants = [
        ('missing_config', 'P8', replace_once(native, 'const auto config = canonicalKey(identity.config);', 'const std::wstring config;'), fixture, 'fixture failed'),
        ('no_kill_on_close', 'P3', replace_once(native, 'JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS', 'JOB_OBJECT_LIMIT_ACTIVE_PROCESS'), fixture, 'abnormal owner death left child residue'),
        ('duplicate_start', 'P1', replace_once(native, 'native_->acquire(identity);', 'try { native_->acquire(identity); } catch (...) {}'), fixture, 'second instance accepted'),
        ('external_kill', 'P6', native, replace_once(fixture, '++counts_.shutdowns;', external_mutation), 'Agent killed unknown external process'),
        ('double_shutdown', 'unit', replace_once(native, 'if (!runtime_->shutdown()) fault = true;', 'if (!runtime_->shutdown()) fault = true;\n        runtime_->shutdown();'), fixture, 'counts.shutdowns == 1'),
    ]
    cmake = ['cmake_minimum_required(VERSION 3.20)', 'project(AgentNegativeControls LANGUAGES CXX)', 'set(CMAKE_CXX_STANDARD 20)']
    for name, _, changed, test, _ in mutants:
        (source / (name + '.cpp')).write_text(changed)
        (source / (name + '_test.cpp')).write_text(test)
        cmake += [f'add_executable({name} {name}.cpp {name}_test.cpp)', f'target_include_directories({name} PRIVATE include)',
                  f'target_compile_definitions({name} PRIVATE VUT_AGENT_TESTING WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)',
                  f'target_link_libraries({name} PRIVATE bcrypt)']
    (source / 'CMakeLists.txt').write_text('\n'.join(cmake) + '\n')
    build = args.work / 'build'
    commands = [[args.cmake, '-S', str(source), '-B', str(build), '-A', 'x64', '-DCMAKE_CXX_FLAGS=/EHsc'],
                [args.cmake, '--build', str(build), '--config', args.config, '--parallel', '4']]
    for i, command in enumerate(commands):
        with open(args.work / f'build-{i}.log', 'w') as out:
            subprocess.run(command, stdout=out, stderr=subprocess.STDOUT, check=True)
    receipts = []
    for name, case, _, _, expected in mutants:
        command = [sys.executable, str(ROOT / 'tests/agent_process_tests.py'), str(build / args.config / (name + '.exe')),
                   '--case', case, '--work', str(args.work / name)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=45)
        log = result.stdout + result.stderr
        (args.work / (name + '.log')).write_text(log)
        detected = result.returncode != 0 and expected in log
        receipts.append({'mutant': name, 'case': case, 'expected_detector': expected, 'exit_code': result.returncode, 'detected': detected})
        (args.work / 'receipts.json').write_text(json.dumps(receipts, indent=2))
        if not detected:
            raise RuntimeError('negative control NOT detected: ' + name)
        print('DETECTED', name, flush=True)

if __name__ == '__main__':
    main()
