#!/usr/bin/env python3
"""Audit Git's staged snapshot before a commit; never print matched secret values."""
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]


def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args])


def audit():
    paths = git('ls-files', '--cached', '-z').decode().split('\0')
    paths = [p for p in paths if p]
    if not paths:
        print('FAIL: no staged/tracked files; run git add on the intended source first.')
        return 1
    forbidden_suffixes = {'.o', '.obj', '.a', '.elf', '.bin', '.8xp', '.8xg', '.8xv',
                          '.map', '.bc', '.pyc', '.dylib', '.so', '.zip', '.dmg', '.pem', '.key'}
    patterns = {
        'private key': r'-----BEGIN (?:[A-Z0-9 ]+ )?PRIVATE KEY-----',
        'GitHub token': r'\b(?:gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,})\b',
        'provider key': r'\bsk-(?:ant-)?[A-Za-z0-9_-]{20,}\b',
        'AWS access key': r'\b(?:AKIA|ASIA)[A-Z0-9]{16}\b',
        'Google API key': r'\bAIza[A-Za-z0-9_-]{30,}\b',
        'Slack token': r'\bxox[baprs]-[A-Za-z0-9-]{15,}\b',
        'credential in URL': r'https?://[^\s/:@]+:[^\s/@]+@',
        'personal home path': r'(?:/Users/|/home/|[A-Z]:\\Users\\)[A-Za-z0-9_.-]+',
        'email/contact address': r'\b[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}\b',
    }
    dummy = {
        'TV_WIFI_SSID': 'COMPILE_ONLY',
        'TV_WIFI_PASSWORD': 'COMPILE_ONLY_NOT_A_PASSWORD',
        'TV_API_KEY': 'COMPILE_ONLY_NOT_A_KEY',
    }
    failures = []
    for path in paths:
        parts = pathlib.PurePosixPath(path).parts
        name = pathlib.PurePosixPath(path).name
        if (pathlib.PurePosixPath(path).suffix.lower() in forbidden_suffixes or
            any(p in {'managed_components', '__pycache__', '.venv', '.tools', 'obj', 'bin'} or
                p.startswith('build-') or p == 'build' for p in parts) or
            name in {'.DS_Store', 'local_config.h', 'board_pins.h', 'sdkconfig', 'sdkconfig.old'} or
            (name.startswith('.env') and name != '.env.example')):
            failures.append((path, 'generated/private file is staged'))
        content = git('show', ':' + path)
        if b'\0' in content:
            failures.append((path, 'binary content is staged'))
            continue
        text = content.decode('utf-8')
        for label, expression in patterns.items():
            if re.search(expression, text):
                failures.append((path, label))
        for match in re.finditer(r'^#define\s+(TV_API_KEY|TV_WIFI_PASSWORD|TV_WIFI_SSID)\s+"([^"\n]*)"', text, re.M):
            key, value = match.groups()
            allowed = path == 'firmware/config/compile_check_config.h' and dummy.get(key) == value
            if value and not allowed:
                failures.append((path, 'nonempty credential/network definition'))
    if failures:
        for path, reason in failures:
            print(f'FAIL: {path}: {reason}')
        return 1
    print(f'PASS: {len(paths)} staged files; no targeted secret, contact/path or generated-file findings.')
    print('Reviewed exceptions: empty local-config template and exact COMPILE_ONLY fixture values.')
    print('Scope: targeted patterns and file checks; manual review is still required.')
    return 0


if __name__ == '__main__':
    sys.exit(audit())
