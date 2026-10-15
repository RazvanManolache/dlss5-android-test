# Build a local test APK

- Clone this repository, then initialize its two direct submodules:

  ```sh
  python tools/init_dependencies.py
  ```

- The helper uses a partial, sparse gallery submodule: only scene source, its asset installer/manifest and licenses are checked out. Desktop comparison screenshots stay remote. It preserves already initialized submodules.
- Standard `git submodule update --init external/phoenix external/gallery` also works, but downloads the desktop screenshots (~77 MiB). Do not add `--recursive`: the gallery's older nested Phoenix is unnecessary here.
- Install Python 3.11–3.13 (3.12 recommended), JDK 17, Gradle 8.11.1, Android SDK platform 35, build-tools 35.0.0, NDK 27.3.13750724 and SDK CMake 3.22.1. Set `JAVA_HOME` and `ANDROID_HOME`.

  ```sh
  python -m pip install -r requirements.txt
  sdkmanager "platforms;android-35" "build-tools;35.0.0" "ndk;27.3.13750724" "cmake;3.22.1" "platform-tools"
  python external/gallery/assets.py prepare
  ```

- Asset preparation downloads the gallery's separate **~972 MiB** asset pack and checks its archive/file hashes. Assets are ignored by the gallery submodule. To reuse an existing prepared gallery, skip the download and pass `--assets-root /path/to/gallery` below.
- Copy `local.example.json` to ignored `local.json`. Supply your own model path and the expected SHA-256; the example identifies the tested model. This project does not download or redistribute it.

  ```sh
  python build.py
  # Or reuse local assets / select explicit tools:
  python build.py --sdk /path/to/Android/Sdk --gradle /path/to/gradle --assets-root /path/to/gallery
  adb install -r dist/Phoenix-Museum-PRIVATE.apk
  ```

- The default ARM64 build downloads the **1.6 MiB, model-free Phoenix 0.8.2 Android preview SDK** from [its release](https://github.com/RazvanManolache/dlss5-phoenix-addon/releases/tag/0.8.2-android-preview.1). Its SHA-256 is pinned in `build.py`; extraction rejects paths outside the build directory. The runtime contains CPU and Vulkan providers, shaders, the C++ runtime and license notices.
- The museum's C++ and Java are compiled locally. Scene layout, actor catalogue and routes are generated from the pinned gallery source. Model hash validation precedes packaging.
- Output is a **private local APK containing your external model and scene derivatives**. `dist/BUILDINFO.json` records dependency commits, selected ABIs and APK hash. Do not upload this APK as a model-free source release. Source licensing does not grant rights to redistribute separate model/asset inputs.
- To compile just Java/C++ in a **fresh checkout**, without any model, assets or SDK download:

  ```sh
  python build.py --code-only
  ```

- This is CI's compile check. Its APK cannot run the museum, and CI does not upload it. A compile pass is not a device or inference test.

## Rebuild Phoenix / use an x86_64 emulator

- Default phone builds use the released SDK. To change native Phoenix code, build the pinned `external/phoenix/native_runtime` instead.
- Provide an existing host **glslang 16.6.0** executable built with `ENABLE_OPT=ON` and **Vulkan-Headers v1.4.365** (directory ending in `include`). Phoenix's [Vulkan CI](https://github.com/RazvanManolache/dlss5-phoenix-addon/blob/b63b5399064aee0665da5d4c92b269cb1da0420c/.github/workflows/standalone.yml) documents those tool builds.

  ```sh
  python build.py --from-source --glslang /path/to/glslang --vulkan-headers /path/to/Vulkan-Headers/include
  # Both architectures, with the same source/tool inputs:
  python build.py --from-source --glslang /path/to/glslang --vulkan-headers /path/to/Vulkan-Headers/include --abi arm64-v8a --abi x86_64
  ```

- Keep toolchains outside this repository. Phoenix owns its backend sources; the direct submodules own the scene and runtime. Do not vendor either into the app.
- Emulator Vulkan uses the host GPU. Its speed or support says nothing about your phone.
- `device_test.py` retains status, screenshot and logs for a settings JSON; `verify_display.py` checks displayed pixels against saved input/output. Both accept `--adb`, `--serial`, `--output`. Use `--help` for details. Disable continually written display proofs before copying them so a capture cannot mix frames.

- This preview includes the Fast GPU implementation. The separately runnable QNN prototypes are described in [FAST_GPU.md](FAST_GPU.md); the normal app build does not download or package QNN.
