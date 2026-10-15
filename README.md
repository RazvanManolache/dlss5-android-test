# Can your phone run DLSS5?

- **Well, it can. It will suck.** Unless I actually manage to optimize things.
- **Phoenix Museum for Android**: experimental reconstruction of DLSS5 neural rendering. A deeply unreasonable workload for a pocket computer.
- Independent project; **not an official NVIDIA Android SDK**. The model DLL is read as data.

![Actual phone capture: original left, completed Phoenix result right](docs/museum.webp)

### Features, including the consequences

- Museum: 316 objects, 16 visitors, two animals. Drag to orbit; pinch to zoom.
- **CPU / Vulkan** switch; optional **64/128-thread compatibility**. No sneaky CPU fallback.
- Split/difference views, freeze comparisons and [all the knobs](docs/APP.md).
- Separate **display / Phoenix FPS** counters. Repainting yesterday's result does not count as new intelligence.

### Your phone versus a PC. Place your bets.

| Measured path / model input | Warm call | Calls/second² |
| --- | ---: | ---: |
| Adreno 735, before optimization · 320×144¹ | 2,010 ms | 0.50 |
| Adreno 735, optimized · 320×144¹ | 1,247 ms | 0.80 |
| RTX 5080, portable Vulkan · 320×144¹ | 77 ms | 12.96 |
| RTX 5080, native RTX host buffers · 1920×1080³ | 8.001 ms | 124.98 |
| RTX 5080, native RTX D3D12 textures · 1920×1080³ | **4.262 ms** | **234.62** |

- **1.61× faster on the phone. Still a slideshow.** Portable Vulkan PC: **16× faster**, identical output bytes.
- Native RTX does **1080p in 4.262 ms**. The phone has requested a transfer to the gift shop.
- Screen-on: **0.65 Phoenix FPS**, **12.24 display FPS**. The museum moves faster than the thinking.
- ¹ One pass, eight blocks once, 320×320 canvas, history reset; six warm calls. Device-specific Vulkan kernels differ.
- ² `1000 / reported warm ms`, not game FPS. [Samples and methods](docs/BENCHMARKS.md).
- ³ Separate synthetic 1080p test; native summaries average process medians. Different input/API: **no matched phone/native speedup claimed**. Host buffers include transfers; textures stay on GPU.

![Measured phone optimization](docs/phone-optimization.png)
![Phone Vulkan, PC portable Vulkan and PC native RTX timings, with separate workload labels](docs/phone-vs-pc.png)

### Want to actually use DLSS? Start here.

- [**Phoenix**](https://github.com/RazvanManolache/dlss5-phoenix-addon): runtime, libraries and serious optimization.
- [**DLSS5 Everything**](https://github.com/RazvanManolache/DLSS5-Everything): Windows games, including the stubborn old ones.
- [**DOSBox Enhanced**](https://github.com/RazvanManolache/dosbox-enhanced): apparently DOS games needed neural rendering too.
- [**Desktop gallery**](https://github.com/RazvanManolache/dlss5-quality-vs-performance-gallery): original museum and quality/performance experiments.

### Small repo. Large ambitions.

- **Pinned submodules**, with lean gallery checkout. Weights, DLLs, APKs and museum assets stay outside Git.
- [Build locally](docs/BUILD.md): Android 9+, ARM64, OpenGL ES 3; Phoenix-compatible compute for Vulkan. Bring patience.
