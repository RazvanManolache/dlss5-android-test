# Can your phone run DLSS5?

- **Well, it can. It will suck.** Unless I actually manage to optimize things.
- This is **Phoenix Museum for Android**: experimental Phoenix reconstruction of DLSS5 neural rendering, a museum, and a deeply unreasonable workload for a pocket computer.
- Independent experiment; **not an official NVIDIA Android SDK**. The model DLL is read as weight data, not executed on Android.

![Actual phone capture: original left, completed Phoenix result right](docs/museum.webp)

### Features, including the consequences

- Native OpenGL ES museum: 316 objects, 16 visitors, two animals. Touch to orbit; pinch to zoom.
- Switch **CPU / Vulkan**. Optional **64/128-thread compatibility** for phones such as the Adreno 735. Unsupported means unsupported; no sneaky CPU fallback.
- Split, single and difference views; freeze comparisons; resolution, presets, lighting, animation and model controls. [The knobs](docs/APP.md).
- Corner counters distinguish **display FPS** from **completed Phoenix FPS**. Repainting yesterday's result does not count as new intelligence.

### Your phone versus a PC. Place your bets.

| Same 320×144 model input¹ | Warm call | Calls/second² |
| --- | ---: | ---: |
| Adreno 735, before optimization | 2,010 ms | 0.50 |
| Adreno 735, optimized | 1,247 ms | 0.80 |
| RTX 5080, portable Vulkan | 77 ms | 12.96 |

- **1.61× faster on the phone. Still a slideshow.** The PC finishes this workload about **16× faster**, with identical output bytes.
- Screen-on app check: **0.65 Phoenix FPS**, **12.24 display FPS**. The museum moves faster than the thinking.
- ¹ One pass, eight blocks once, 320×320 working canvas, history reset. Six warm calls per headless run; first call excluded. Device-specific Vulkan kernels differ.
- ² `1000 / median call ms`, not game FPS. [Raw samples, methods and limits](docs/BENCHMARKS.md).

![Measured phone optimization](docs/phone-optimization.png)
![Measured PC versus phone model-call time](docs/phone-vs-pc.png)

### Want to actually use DLSS? Start here.

- [**Phoenix**](https://github.com/RazvanManolache/dlss5-phoenix-addon): runtime, libraries, backends and the serious optimization work.
- [**DLSS5 Everything**](https://github.com/RazvanManolache/DLSS5-Everything): getting it into Windows games, including the stubborn old ones.
- [**DOSBox Enhanced**](https://github.com/RazvanManolache/dosbox-enhanced): because apparently DOS games needed neural rendering too.
- [**Desktop gallery**](https://github.com/RazvanManolache/dlss5-quality-vs-performance-gallery): the original museum and quality/performance experiments.

### Small repo. Large ambitions.

- Phoenix and the gallery are **pinned Git submodules**. No copied dependency trees.
- No weights, DLLs, APKs or museum asset pack in Git. Assets are a separate optional download; your model stays local.
- [Build your private test APK](docs/BUILD.md). Android 9+, ARM64, OpenGL ES 3; Vulkan inference needs Phoenix-compatible compute features. Bring patience.
