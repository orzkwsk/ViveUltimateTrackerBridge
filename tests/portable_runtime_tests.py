"""Windows launcher regressions; copied scripts only, no product or HID execution."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(os.name == 'nt', 'Windows command/PowerShell entrypoints')
class PortableRuntimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = ROOT / 'Build' / 'Tests' / ('portable-runtime-' + uuid.uuid4().hex)
        cls.work.mkdir(parents=True)
        cls.ps = Path(os.environ['WINDIR']) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
        source = cls.work / 'Capture.cs'
        source.write_text('''using System; using System.IO;
class Capture { static int Main(string[] args) {
File.AppendAllText(Environment.GetEnvironmentVariable("CAPTURE_PATH"), string.Join("|", args)+"\\n");
return int.Parse(Environment.GetEnvironmentVariable("STUB_EXIT") ?? "0"); } }''')
        compiler = Path(os.environ['WINDIR']) / 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
        cls.stub = cls.work / 'capture.exe'
        subprocess.run([str(compiler), '/nologo', '/out:' + str(cls.stub), str(source)], check=True)

    def setUp(self):
        self.repo = self.work / ('Relocated checkout with spaces ' + uuid.uuid4().hex)
        (self.repo / 'scripts').mkdir(parents=True)
        self.repo = self.repo.resolve()
        self.foreign = self.work / 'foreign cwd'
        self.foreign.mkdir(exist_ok=True)
        for name in ('start_vive_backend.ps1', 'build_runtime.ps1', 'vive_launcher_process.ps1'):
            shutil.copy2(ROOT / 'scripts' / name, self.repo / 'scripts' / name)
        for name in ('Start_VIVE_Backend.cmd', 'Build_Runtime.cmd'):
            shutil.copy2(ROOT / name, self.repo / name)
        self.capture = self.repo / 'calls.txt'
        self.env = os.environ | {'CAPTURE_PATH': str(self.capture), 'STUB_EXIT': '0'}

    def prepared(self):
        config = self.repo / 'config/vut-service.conf'
        config.parent.mkdir(exist_ok=True)
        config.write_text('source fixture-installation\nmap 10:11:12:13:14:15 fixture-space\n')
        exe = self.repo / 'Build/runtime/Release/vut_service.exe'
        exe.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(self.stub, exe)

    def invoke(self, script='start_vive_backend.ps1', args=(), code=0):
        def quote(text):
            return "'" + str(text).replace("'", "''") + "'"
        wrapper = self.repo / 'dispatch.ps1'
        wrapper.write_text("$ErrorActionPreference='Stop'\n"
                           "$env:LOCALAPPDATA=" + quote(self.repo / 'state/local') + "\n"
                           "function Get-CimInstance { @() }\n"
                           "& " + quote(self.repo / 'scripts' / script) + ' ' +
                           ' '.join(x if x.startswith('-') else quote(x) for x in args) + "\nexit $LASTEXITCODE\n")
        result = subprocess.run([str(self.ps), '-NoProfile', '-ExecutionPolicy', 'Bypass',
                                 '-File', str(wrapper)], cwd=self.foreign, env=self.env,
                                capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, code, result.stdout + result.stderr)
        return result

    def test_missing_prerequisites_are_aggregated_without_writes(self):
        result = self.invoke(args=('-Check',), code=1)
        self.assertIn('Missing configuration:', result.stderr)
        self.assertIn('Missing executable:', result.stderr)
        self.assertIn('Build_Runtime.cmd', result.stderr)
        self.assertFalse((self.repo / 'state').exists())
        self.assertFalse(self.capture.exists())

    def test_check_is_read_only_and_explicit_about_its_limits(self):
        self.prepared()
        result = self.invoke(args=('-Check',))
        report = json.loads(result.stdout)
        self.assertEqual(report['status'], 'FILES_PRESENT')
        self.assertFalse(report['process_started'])
        self.assertFalse(report['configuration_validated'])
        self.assertFalse(report['runtime_readiness_verified'])
        self.assertFalse((self.repo / 'state').exists())
        self.assertFalse(self.capture.exists())

    def test_relative_journal_is_relative_to_the_repository(self):
        self.prepared()
        result = self.invoke(args=('-JournalPath', 'state/custom journal.log', '-Check'))
        self.assertEqual(Path(json.loads(result.stdout)['journal']), self.repo / 'state/custom journal.log')

    def test_placeholder_and_unknown_arguments_fail_before_execution(self):
        self.prepared()
        (self.repo / 'config/vut-service.conf').write_text('source vive-installation-example\n')
        self.assertIn('placeholder', self.invoke(args=('-Check',), code=1).stderr)
        self.assertIn('Unknown arguments', self.invoke(args=('-BuildConfig', 'Release', '-Unexpected'), code=1).stderr)
        self.assertFalse(self.capture.exists())

    def test_normal_launcher_preserves_arguments_and_exit_with_a_stub(self):
        self.prepared()
        self.env['STUB_EXIT'] = '37'
        self.invoke(args=('-JournalPath', 'state/journal with spaces.log', '-Port', '31111',
                          '-Diagnostics', '-NoBuild'), code=37)
        self.assertEqual(self.capture.read_text().splitlines(), [
            '|'.join((str(self.repo / 'config/vut-service.conf'),
                      str(self.repo / 'state/journal with spaces.log'), '31111', '--diagnostics'))])

    def test_batch_entry_rejects_unknown_or_extra_arguments(self):
        for args in ('--invalid', '--check extra'):
            result = subprocess.run(f'"{os.environ["COMSPEC"]}" /d /s /c ""{self.repo / "Start_VIVE_Backend.cmd"}" {args}"',
                                    cwd=self.foreign, env=self.env, input='\n', capture_output=True,
                                    text=True, timeout=20)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertFalse(self.capture.exists())

    def test_explicit_builder_uses_one_fixed_output_tree(self):
        self.env['MONAKA_CMAKE'] = str(self.stub)
        self.invoke(script='build_runtime.ps1')
        calls = self.capture.read_text().splitlines()
        self.assertEqual(len(calls), 2)
        self.assertIn('|-B|' + str(self.repo / 'Build/runtime') + '|', calls[0])
        self.assertTrue(calls[1].startswith('--build|' + str(self.repo / 'Build/runtime') + '|'))

    def test_failed_configure_is_not_followed_by_build(self):
        self.env.update(MONAKA_CMAKE=str(self.stub), STUB_EXIT='23')
        self.invoke(script='build_runtime.ps1', code=23)
        self.assertEqual(len(self.capture.read_text().splitlines()), 1)


if __name__ == '__main__':
    unittest.main(verbosity=2)
