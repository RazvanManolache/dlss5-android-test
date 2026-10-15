"""Fail if private/generated payloads enter the public source snapshot."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
paths = subprocess.check_output(['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard'], cwd=ROOT).decode().split('\0')
total = 0
count = 0
errors = []
for name in sorted(set(paths) - {''}):
    path = ROOT/name
    if path.is_dir():  # gitlink: dependency owns its source/license boundary
        continue
    if not path.exists():
        errors.append('Missing source file: '+name)
        continue
    size = path.stat().st_size
    total += size
    count += 1
    if size > 512*1024:
        errors.append('File exceeds 512 KiB source limit: '+name)
    if path.suffix.lower() in {'.dll','.so','.apk','.aab','.exe','.zip','.mesh','.skin','.bones','.rgba','.rgb32f','.rg32f','.spv'}:
        errors.append('Binary/model/scene payload: '+name)
    if name.startswith(('app/src/main/assets/', 'app/src/main/jniLibs/', 'evidence/', 'dist/', 'build/')) or name in {'local.json','local.properties'}:
        errors.append('Private/generated path: '+name)
if total > 2*1024*1024:
    errors.append('Source payload exceeds 2 MiB')
pins = {'external/phoenix':'4c9b75ff42b6945352f66f309dc84404e498f4cf', 'external/gallery':'c077add7e3c77b84792927a82cbcdbbd4aac9692'}
for name, expected in pins.items():
    actual = subprocess.check_output(['git', '-C', str(ROOT/name), 'rev-parse', 'HEAD'], text=True).strip()
    if actual != expected:
        errors.append('Unexpected dependency commit: '+name)
if errors:
    raise SystemExit('\n'.join(errors))
print(f'Source boundary passed: {count} files, {total:,} bytes; two pinned submodules; no model, APK or scene payload.')
