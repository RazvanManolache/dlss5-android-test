"""Initialize pinned submodules without downloading desktop gallery screenshots."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def git(*args, cwd=ROOT, **kwargs):
    return subprocess.run(['git', *args], cwd=cwd, check=True, **kwargs)


def main():
    git('submodule', 'init', 'external/phoenix', 'external/gallery')
    for name in ['phoenix', 'gallery']:
        relative = 'external/'+name
        path = ROOT/relative
        if (path/'.git').exists():
            print(relative+': already initialized; preserving its working tree')
            continue
        url = subprocess.check_output(['git', 'config', '-f', '.gitmodules', 'submodule.'+relative+'.url'], cwd=ROOT, text=True).strip()
        index = subprocess.check_output(['git', 'ls-files', '--stage', relative], cwd=ROOT, text=True).split()
        if not index or index[0] != '160000':
            raise RuntimeError('Expected a pinned submodule: '+relative)
        commit = index[1]
        if name == 'gallery':
            git('clone', '--filter=blob:none', '--no-checkout', url, str(path))
            # Only these tracked files are consumed by generation, asset setup
            # and attribution. The large desktop docs/ image tree stays remote.
            patterns = ['/gallery_people.json', '/heavy_pipeline.inc', '/visitor_scene.inc',
                '/assets.py', '/gallery_assets.json', '/CREDITS.md', '/LICENSE',
                '/THIRD_PARTY_NOTICES.md', '/licenses/']
            git('sparse-checkout', 'set', '--no-cone', '--stdin', cwd=path,
                input='\n'.join(patterns)+'\n', text=True)
            git('checkout', '--detach', commit, cwd=path)
            git('submodule', 'absorbgitdirs', '--', relative)
        else:
            git('submodule', 'update', '--init', '--', relative)
        actual = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=path, text=True).strip()
        if actual != commit:
            raise RuntimeError('Dependency revision differs from gitlink: '+relative)
        print(relative+': '+commit[:12])


    # Keep the audited SDK revision; apply only reviewed compiler compatibility fixes.
    phoenix = ROOT/'external/phoenix'
    expected = subprocess.check_output(['git', 'ls-files', '--stage', 'external/phoenix'], cwd=ROOT, text=True).split()[1]
    actual = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=phoenix, text=True).strip()
    if actual != expected:
        raise RuntimeError('Phoenix revision differs from pinned gitlink')
    patch = str(ROOT/'tools/phoenix-compiler-compat.patch')
    applied = subprocess.run(['git', 'apply', '--reverse', '--check', patch], cwd=phoenix, capture_output=True)
    if applied.returncode != 0:
        git('apply', '--check', patch, cwd=phoenix)
        git('apply', patch, cwd=phoenix)
    print('Phoenix compiler compatibility patch ready')


if __name__ == '__main__':
    main()
