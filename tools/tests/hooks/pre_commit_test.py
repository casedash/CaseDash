from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]


@unittest.skipUnless(os.name == 'nt', 'CaseDash hooks require Windows')
class PreCommitTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory(prefix='hook_', dir=REPO_ROOT / 'build')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in ('format.cmd', 'tools/git_file_list.ps1', 'tools/pre_commit_checks.ps1'):
            target = self.root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(REPO_ROOT / name, target)
        (self.root / 'build').mkdir()
        shutil.copyfile(REPO_ROOT / 'build/CaseDashTools.exe', self.root / 'build/CaseDashTools.exe')
        self.write('build.cmd', '@echo off\necho built>build/rebuilt.txt\nexit /b 0\n')
        self.write('lint.cmd', '@echo off\nexit /b 0\n')
        self.write('.gitignore', 'build/\n')
        self.write('.cpp-format', 'ColumnLimit: 120\n')
        self.write('.cpp-format-ignore', 'build\nexternal\n')
        self.write('example.cpp', 'int value = 1;\n')
        self.write('notes.txt', 'original\n')
        self.git('init', '-q')
        self.git('config', 'user.name', 'Hook test')
        self.git('config', 'user.email', 'hook-test@example.invalid')
        self.git('config', 'core.autocrlf', 'false')
        self.git('config', 'core.hooksPath', '.no-hooks')
        self.git('add', '.')
        self.git('commit', '-qm', 'Initial fixture')

    def write(self, path: str, text: str) -> None:
        target = self.root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(text.replace('\n', '\r\n').encode())

    def git(self, *args: str) -> str:
        result = subprocess.run(['git', *args], cwd=self.root, capture_output=True, text=True, check=True)
        return result.stdout

    def hook(self, expected: int = 0) -> None:
        result = subprocess.run(
            ['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
             str(self.root / 'tools/pre_commit_checks.ps1')],
            cwd=self.root, capture_output=True, text=True,
        )
        self.assertEqual(expected, result.returncode, result.stdout + result.stderr)

    def stage_unformatted(self) -> None:
        self.write('example.cpp', 'int value=2;\n')
        self.git('add', 'example.cpp')

    def test_formats_and_restages_with_existing_tool(self) -> None:
        self.stage_unformatted()
        self.hook()
        self.assertEqual('int value = 2;\n', self.git('show', ':example.cpp'))
        self.assertTrue((self.root / 'build/rebuilt.txt').exists())
        self.assertEqual('', self.git('diff'))

    def test_restores_unstaged_and_untracked_files_and_keeps_older_stash(self) -> None:
        self.write('notes.txt', 'older stash\n')
        self.git('stash', 'push', '-qm', 'unrelated')
        old_stash = self.git('rev-parse', 'refs/stash')
        self.stage_unformatted()
        self.write('notes.txt', 'unstaged work\n')
        self.write('new.txt', 'untracked work\n')
        self.hook()
        self.assertEqual('int value = 2;\n', self.git('show', ':example.cpp'))
        self.assertEqual('unstaged work\n', (self.root / 'notes.txt').read_text())
        self.assertEqual('original\n', self.git('show', ':notes.txt'))
        self.assertEqual('untracked work\n', (self.root / 'new.txt').read_text())
        self.assertEqual(old_stash, self.git('rev-parse', 'refs/stash'))

    def test_preserves_partial_staging_in_same_source(self) -> None:
        original = 'int value = 1;\n' + '// context\n' * 20 + 'int other = 1;\n'
        self.write('example.cpp', original)
        self.git('add', 'example.cpp')
        self.git('commit', '-qm', 'Partial staging fixture')
        self.write('example.cpp', original.replace('value = 1', 'value=2'))
        self.git('add', 'example.cpp')
        self.write('example.cpp', original.replace('value = 1', 'value=2').replace('other = 1', 'other = 3'))
        self.hook()
        self.assertEqual(original.replace('value = 1', 'value = 2'), self.git('show', ':example.cpp'))
        self.assertEqual(
            original.replace('value = 1', 'value = 2').replace('other = 1', 'other = 3'),
            (self.root / 'example.cpp').read_text(),
        )
        self.assertEqual('', self.git('stash', 'list'))

    def test_restores_unstaged_work_when_lint_fails(self) -> None:
        self.write('lint.cmd', '@echo off\nexit /b 7\n')
        self.git('add', 'lint.cmd')
        self.stage_unformatted()
        self.write('notes.txt', 'unstaged work\n')
        self.hook(7)
        self.assertEqual('unstaged work\n', (self.root / 'notes.txt').read_text())
        self.assertEqual('', self.git('stash', 'list'))

    def test_config_only_commit_formats_existing_sources_and_ignores_dependencies(self) -> None:
        self.stage_unformatted()
        self.write('external/ignored.cpp', 'not valid C++\n')
        self.git('add', 'external/ignored.cpp')
        self.git('commit', '-qm', 'Unformatted fixture')
        self.write('.cpp-format', 'ColumnLimit: 80\n')
        self.git('add', '.cpp-format')
        self.hook()
        self.assertEqual('int value = 2;\n', self.git('show', ':example.cpp'))
        self.assertEqual('not valid C++\n', self.git('show', ':external/ignored.cpp'))

    def test_preserves_stash_when_restoring_same_line_conflicts(self) -> None:
        self.stage_unformatted()
        self.write('example.cpp', 'int value=3;\n')
        self.hook(1)
        self.assertIn('casedash-pre-commit-format', self.git('stash', 'list'))
        self.assertEqual('int value=3;\n', self.git('show', 'stash@{0}:example.cpp'))


if __name__ == '__main__':
    unittest.main(verbosity=2)
