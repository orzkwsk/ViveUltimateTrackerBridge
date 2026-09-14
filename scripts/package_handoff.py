"""Package committed Task 3 handoff without depending on sibling source trees."""
import hashlib,json,subprocess,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def git(*args):return subprocess.check_output(['git',*args],cwd=ROOT).decode().strip()
def sha(data):return hashlib.sha256(data).hexdigest()
if git('status','--porcelain'):raise SystemExit('Commit reviewed source before packaging')
subprocess.run(['python',str(ROOT/'scripts/import_protocol_kit.py'),'--verify'],cwd=ROOT,check=True)
head=git('rev-parse','HEAD');dist=ROOT/'dist';dist.mkdir(exist_ok=True)
samples=dist/'vive-status-samples.jsonl'
subprocess.run([str(ROOT/'build/Release/vut_observation_tests.exe'),'--emit',str(samples)],check=True)
paths=['dependencies/artifacts/monaka-protocol-kit-v1.0.zip','dependencies/monaka-protocol.handoff.json',
       'dependencies/monaka-protocol.lock.json','dependencies/task1-external-handoff.json',
       'docs/task3/implementation.md','config/vut-service.example.conf','tests/fixtures/pose37.bin',
       'tests/fixtures/pose39.bin','tests/mock_consumer.hpp','tests/mock_consumer.cpp']
files={p:subprocess.check_output(['git','show',head+':'+p],cwd=ROOT) for p in paths}
files['samples/vive-status-samples.jsonl']=samples.read_bytes()
artifact=dist/'vive-backend-handoff.zip'
with zipfile.ZipFile(artifact,'w',compression=zipfile.ZIP_DEFLATED) as z:
    for name,data in sorted(files.items()):
        info=zipfile.ZipInfo(name,date_time=(2026,1,1,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED
        z.writestr(info,data)
manifest={'artifact':artifact.name,'sha256':sha(artifact.read_bytes()),'source_commit':head,
          'contract_status':'candidate / master reconciliation pending','synthetic_fixtures':True,
          'files':{p:sha(data) for p,data in sorted(files.items())}}
manifest_path=dist/'vive-backend-handoff.handoff.json';manifest_path.write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
meta=json.loads((ROOT/'dependencies/monaka-protocol.handoff.json').read_text())
base='78eadab2ff7a159a08679bb4a70a4d35b18985e6'
base=git('rev-parse',base)
changes=[]
for name in git('diff','--name-only',base,head).splitlines():
    if name.startswith('third_party/'):reason='Exact fixed Task 1 kit bytes, including codec/schema/fixtures/licenses'
    elif name.startswith('dependencies/'):reason='Fixed artifact and external/internal provenance'
    elif name.startswith('tests/'):reason='Release regression, generic consumer, persistence/UDP or cross-language verification'
    elif name.startswith('docs/') or name=='README.md':reason='Ownership, mapping/limits, reproducible verification and Task 4 handoff'
    elif name.startswith('scripts/'):reason='Reproducible artifact integrity/package tooling'
    elif 'observation_' in name:reason='Vendor-to-C1 admission or bounded nonblocking publication'
    elif name=='src/protocol.cpp':reason='MSVC endian portability and separate half subnormal correction'
    elif name=='src/backend.cpp' or name.startswith('include/vut/'):reason='Immutable HID receipt/generation and transport failure lifetime fix'
    elif name.startswith('tools/') or name.startswith('config/'):reason='Windows service composition, explicit persistent identity/maps and revision journal'
    else:reason='Build targets, exact-byte import attributes or generated-output ignores'
    changes.append({'path':name,'reason':reason})
report={
 'repository':'orzkwsk/ViveUltimateTrackerBridge','audited_base_branch':'dev/vivehub-bypass',
 'audited_base_sha':'db7c6795055fc00054b43a64e3bbd658a3575932',
 'actual_base_branch':'origin/refactor/monaka-layer-separation','actual_base_sha':base,
 'base_change_reason':'Prepared branch adds AGENTS/task specification/handoff metadata to audited ancestor; main was not implementation base',
 'work_branch':git('branch','--show-current'),'HEAD_SHA':head,'protocol_version':'1.0 candidate / master reconciliation pending',
 'schema_commit':meta['schema_commit'],'protocol_kit_sha256':meta['sha256'],'contract_c1_sha256':meta['contract_c1_sha256'],
 'changed_files':changes,
 'build_result':{'status':'PASS','environment':'Windows 11 x64, MSVC 19.51.36257.0, SDK 10.0.26100.0, CMake 4.3.1-msvc1',
   'commands':['cmake -S . -B build -G "Visual Studio 18 2026" -A x64',
    'cmake -S . -B build -DJava_JAVA_EXECUTABLE="C:/Program Files/Microsoft/jdk-17.0.20.101-hotspot/bin/java.exe" -DJava_JAVAC_EXECUTABLE="C:/Program Files/Microsoft/jdk-17.0.20.101-hotspot/bin/javac.exe"',
    'cmake --build build --config Release','ctest --test-dir build -C Release --output-on-failure',
    'cmake -S . -B build-diagnostic -G "Visual Studio 18 2026" -A x64 -DVUT_BUILD_OBSERVATION=OFF',
    'cmake --build build-diagnostic --config Release','ctest --test-dir build-diagnostic -C Release --output-on-failure'],
   'execution_notes':'Reviewed host execution required after sandbox MSVC discovery failure. Java 8 autodetection overridden to installed JDK17. UTF-8 explicitly selected after test harness encoding failure.'},
 'unit_test_result':{'status':'PASS','release_ctest':'6/6','diagnostic_only_ctest':'1/1','negative_control':'Release --negative-fixture returned 1 with failed 0x0001 == 0 check',
    'half_fix_commit':'1b2b172','pre_fix_regression':'Release test failed before bias correction; same boundary tests pass after correction'},
 'mock_or_cross_language_result':{'status':'PASS','fixed_fixtures':74,'shared_cross_language_directions':44,'vive_cpp_jvm_cpp_status_samples':7,
    'udp':'1000 fixture emissions while receiver absent; receiver stop/reopen twice; producer completes',
    'backpressure':'10000 injected send failures, bounded queue, encode failure recovery',
    'configuration':'Persistent increasing revision, new UUID, duplicate MAC, torn/corrupt journal tests; no HID opened'},
 'hardware_validation':{item:'NOT RUN' for item in ['Hub-stopped official dongle open','one tracker','multiple trackers','optical loss/recovery','unpair/power-cycle','dongle disconnect/reconnect','map reset/restart','1m translation','axis/quaternion convention']},
 'compatibility_status':'Backend API/probe/C++20 and official HID/feature/ACK/37-39-byte paths retained; diagnostic-only build passes. Appended event fields require rebuild, no binary ABI promise.',
 'phase_or_DoD_status':'Task 3 software implementation and C1 integration complete; hardware and Linux validation NOT RUN; native profile remains unverified',
 'unresolved_issues':['C1 master reconciliation pending','Native axes/metres/quaternion/map sharing not hardware verified','Short RF counters cannot establish true sensor/driver-buffer age or arbitrary full-cycle delayed replay; gaps >500ms fail closed until restart','Undetected map reset requires operator restart','Battery/derivative semantics and map-transfer recovery unresolved','Linux NOT RUN'],
 'followup_required_in_other_repos':['Task4: generic consumer age/session/retired handling, identity mapping and verified native profile/calibration/output policy','MonakaVR: body roles, fusion/fallback/IK','Task1-5: synchronized master contract reconciliation'],
 'handoff_artifacts':[{'filename':artifact.name,'sha256':manifest['sha256'],'source_commit':head},
   {'filename':manifest_path.name,'sha256':sha(manifest_path.read_bytes()),'source_commit':head},
   {'filename':samples.name,'sha256':sha(samples.read_bytes()),'source_commit':head}],
 'responsibilities':{'backend':'official dongle/HID, independent decode, vendor diagnostics, observation mapping/publication',
   'upper_layers':'verified transforms/calibration, ID mapping, consumer freshness and output routing, body/solver policies',
   'legacy_evidence':'Existing targets and method names remain; C1-off Windows Release protocol test passes; hardware behavior not claimed'}}
(dist/'task3-final-report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'source_commit':head,'artifact_sha256':manifest['sha256'],'manifest_sha256':sha(manifest_path.read_bytes())},indent=2))
