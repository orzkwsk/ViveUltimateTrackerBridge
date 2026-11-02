"""Explicit builder regressions; operational entry tests live outside this repository."""
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
        for name in ('build_runtime.ps1',):
            shutil.copy2(ROOT / 'scripts' / name, self.repo / 'scripts' / name)
        for name in ('Build_Runtime.cmd',):
            shutil.copy2(ROOT / name, self.repo / name)
        self.capture = self.repo / 'calls.txt'
        self.env = os.environ | {'CAPTURE_PATH': str(self.capture), 'STUB_EXIT': '0'}


    def invoke(self, script='build_runtime.ps1', args=(), code=0):
        def quote(text):
            return "'" + str(text).replace("'", "''") + "'"
        wrapper = self.repo / 'dispatch.ps1'
        wrapper.write_text("$ErrorActionPreference='Stop'\n"
                           "$env:LOCALAPPDATA=" + quote(self.repo / 'state/local') + "\n"
                           "& " + quote(self.repo / 'scripts' / script) + ' ' +
                           ' '.join(x if x.startswith('-') else quote(x) for x in args) + "\nexit $LASTEXITCODE\n")
        result = subprocess.run([str(self.ps), '-NoProfile', '-ExecutionPolicy', 'Bypass',
                                 '-File', str(wrapper)], cwd=self.foreign, env=self.env,
                                capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, code, result.stdout + result.stderr)
        return result


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
