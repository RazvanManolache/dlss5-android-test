"""Build a local museum APK. Dependencies and generated data stay outside Git."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parent
SDK_VERSION = '0.8.1-beta'
SDK_SHA256 = '2731b260edde4f5c9be1c80806637b3ff32eaceef6e33551f730649685037107'
NDK_VERSION = '27.3.13750724'


def release_sdk():
    """Download the pinned, model-free ARM64 SDK and authenticate its archive."""
    cache = ROOT / 'build/downloads'
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / f'DLSS5-Phoenix-SDK-{SDK_VERSION}-android-arm64.zip'
    if not archive.exists():
        url = f'https://github.com/RazvanManolache/dlss5-phoenix-addon/releases/download/{SDK_VERSION}/{archive.name}'
        partial = archive.with_suffix('.partial')
        print('Downloading model-free Phoenix SDK (3.7 MiB)', flush=True)
        urllib.request.urlretrieve(url, partial)
        partial.replace(archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != SDK_SHA256:
        raise ValueError('SDK archive checksum mismatch; remove it and retry')
    target = ROOT / 'build/released-sdk'
    with zipfile.ZipFile(archive) as package:
        for item in package.infolist():
            candidate = (target / item.filename).resolve()
            if not candidate.is_relative_to(target.resolve()):
                raise ValueError('Unsafe SDK archive entry')
        package.extractall(target)
    return target


def source_sdk(args, abi, sdk):
    if not args.glslang or not args.vulkan_headers:
        raise ValueError('--from-source needs --glslang and --vulkan-headers; see docs/BUILD.md')
    suffix = '.exe' if os.name == 'nt' else ''
    cmake = sdk / f'cmake/3.22.1/bin/cmake{suffix}'
    output = ROOT / 'build' / abi
    install = ROOT / 'build' / ('sdk-' + abi)
    subprocess.run([str(cmake), '-S', str(ROOT/'external/phoenix/native_runtime'), '-B', str(output), '-G', 'Ninja',
        '-DCMAKE_MAKE_PROGRAM='+str(sdk/f'cmake/3.22.1/bin/ninja{suffix}'), '-DCMAKE_BUILD_TYPE=Release',
        '-DCMAKE_TOOLCHAIN_FILE='+str(sdk/f'ndk/{NDK_VERSION}/build/cmake/android.toolchain.cmake'),
        '-DANDROID_ABI='+abi, '-DANDROID_PLATFORM=android-28', '-DANDROID_STL=c++_shared',
        '-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON', '-DCMAKE_SHARED_LINKER_FLAGS=-Wl,-z,max-page-size=16384',
        '-DPHOENIX_ENABLE_METAL=OFF', '-DPHOENIX_ENABLE_VULKAN=ON',
        '-DPHOENIX_VULKAN_HEADERS='+str(args.vulkan_headers.resolve()),
        '-DPHOENIX_GLSLANG='+str(args.glslang.resolve()), '-DPython3_EXECUTABLE='+sys.executable,
        '-DBUILD_TESTING=OFF'], check=True)
    subprocess.run([str(cmake), '--build', str(output), '--parallel', '4'], check=True)
    subprocess.run([str(cmake), '--install', str(output), '--prefix', str(install)], check=True)
    # Older install rules omit the NDK's shared C++ runtime.
    host = {'Windows': 'windows-x86_64', 'Linux': 'linux-x86_64', 'Darwin': 'darwin-x86_64'}[platform.system()]
    triple = {'arm64-v8a': 'aarch64-linux-android', 'x86_64': 'x86_64-linux-android'}[abi]
    shutil.copy2(sdk/f'ndk/{NDK_VERSION}/toolchains/llvm/prebuilt/{host}/sysroot/usr/lib/{triple}/libc++_shared.so', install/'lib/libc++_shared.so')
    phoenix = ROOT/'external/phoenix'
    for name in ['LICENSE', 'NOTICE', 'THIRD_PARTY.md']:
        shutil.copy2(phoenix/name, install/name)
    licenses = install/'licenses'
    licenses.mkdir(exist_ok=True)
    for dependency in ['amd_nr', 'intel_nr', 'portable_nr']:
        for name in ['LICENSE', 'NOTICE', 'PROVENANCE.md']:
            target = licenses/dependency/name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(phoenix/'third_party'/dependency/name, target)
    shutil.copy2(sdk/f'ndk/{NDK_VERSION}/NOTICE', licenses/'Android-NDK-NOTICE')
    shutil.copy2(ROOT/'licenses/nlohmann-json-MIT.txt', licenses/'nlohmann-json-MIT.txt')
    return install


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, default=os.environ.get('ANDROID_HOME') or os.environ.get('ANDROID_SDK_ROOT'))
    parser.add_argument('--gradle', help='Gradle 8.11.1 executable; defaults to PATH/GRADLE_HOME or existing wrapper cache')
    parser.add_argument('--assets-root', type=Path, default=ROOT/'external/gallery')
    parser.add_argument('--local', type=Path, default=ROOT/'local.json')
    parser.add_argument('--code-only', action='store_true', help='Compile check only: produces no runnable museum and is never distributed')
    parser.add_argument('--from-source', action='store_true', help='Build Phoenix instead of using its checksum-pinned SDK')
    parser.add_argument('--abi', action='append', choices=['arm64-v8a', 'x86_64'])
    parser.add_argument('--glslang', type=Path)
    parser.add_argument('--vulkan-headers', type=Path, help='Directory containing vulkan/vulkan.h')
    args = parser.parse_args()
    if not args.sdk:
        parser.error('Set ANDROID_HOME or pass --sdk')
    sdk = args.sdk.resolve()
    abis = args.abi or ['arm64-v8a']
    if 'x86_64' in abis and not args.from_source and not args.code_only:
        parser.error('x86_64 needs --from-source (the released Android SDK is ARM64)')
    if not (ROOT/'external/phoenix/native_runtime/phoenix.h').exists() or not (ROOT/'external/gallery/gallery_people.json').exists():
        parser.error('Initialize external/phoenix and external/gallery submodules first')
    import prepare
    if args.code_only:
        # Prevent a previous private build leaking into a model-free CI check.
        if (ROOT/'app/src/main/assets').exists() or (ROOT/'app/src/main/jniLibs').exists():
            parser.error('--code-only requires a clean checkout with no generated assets or jniLibs')
        prepare.generate_scene()
    else:
        if not args.local.exists():
            parser.error('Copy local.example.json to local.json and configure your separately supplied model')
        prepare.ASSET_ROOT = args.assets_root.resolve()
        prepare.LOCAL = args.local.resolve()
        prepare.RUNTIMES = {abi: source_sdk(args, abi, sdk) if args.from_source else release_sdk() for abi in abis}
        # Never package stale libraries from a previously selected ABI.
        libs = ROOT/'app/src/main/jniLibs'
        if libs.exists():
            for directory in libs.iterdir():
                if directory.is_dir() and directory.name not in abis:
                    shutil.rmtree(directory)
        prepare.prepare()
    (ROOT/'local.properties').write_text('sdk.dir='+sdk.as_posix()+'\n')
    executable = 'gradle.bat' if os.name == 'nt' else 'gradle'
    gradle = args.gradle or shutil.which(executable)
    if not gradle and os.environ.get('GRADLE_HOME'):
        gradle = str(Path(os.environ['GRADLE_HOME'])/'bin'/executable)
    if not gradle:
        gradle = next((Path.home()/'.gradle/wrapper/dists/gradle-8.11.1-bin').glob('*/gradle-8.11.1/bin/'+executable), None)
    if not gradle:
        parser.error('Install Gradle 8.11.1 or pass --gradle /path/to/gradle')
    subprocess.run([str(gradle), '--no-daemon', '-PmuseumAbis='+','.join(abis), ':app:assembleDebug'], cwd=ROOT, check=True)
    if args.code_only:
        print('Model-free Java/C++ compile check passed. This APK is not a runnable museum.')
        return
    dist = ROOT/'dist'
    dist.mkdir(exist_ok=True)
    apk = dist/'Phoenix-Museum-PRIVATE.apk'
    shutil.copy2(ROOT/'app/build/outputs/apk/debug/app-debug.apk', apk)
    digest = hashlib.sha256(apk.read_bytes()).hexdigest()
    (dist/'SHA256SUMS.txt').write_text(digest+'  '+apk.name+'\n')
    def commit(path):
        return subprocess.check_output(['git', '-C', str(path), 'rev-parse', 'HEAD'], text=True).strip()
    (dist/'BUILDINFO.json').write_text(json.dumps({'apk': apk.name, 'sha256': digest, 'abis': abis,
        'contains_external_model': True, 'distribution': 'local private test',
        'phoenix_commit': commit(ROOT/'external/phoenix'), 'gallery_commit': commit(ROOT/'external/gallery'),
        'runtime': 'source build' if args.from_source else SDK_VERSION}, indent=2))
    print('Built local private APK:', apk)


if __name__ == '__main__':
    main()
