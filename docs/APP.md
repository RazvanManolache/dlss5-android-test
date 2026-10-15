# Using the museum

Open **Phoenix Museum**. Drag the scene to orbit; pinch to zoom. The collapsible
menu has CPU/Vulkan selection, seven mobile rendering presets, independent
left/right Phoenix and body-detail choices, split/single/difference views, person
and animal cameras, framing, animation/light/orbit toggles, and Phoenix controls.
Controls include intensity, local tone/structure, skin structure, automatic mask,
style, seed, passes, eight block repetition counts, CPU threads and history reset.

Use **Freeze & compare original / Phoenix** at the top of the menu to request an
off/on pair. Persistent labels describe the pixels actually displayed: original,
waiting with original still shown, unavailable, or a numbered Phoenix result.
The selected backend alone does not mean a neural result is on screen. The
**Compare at the same processing resolution** option makes both sides use the
same sampled source size, separating model changes from downsampling blur.
Changing the view or this display option retains the completed result.
**Resume original live scene** returns to the native-resolution moving view.

The scene renders at the display's native dimensions. **Processing resolution**
only changes the image sent to Phoenix. A 160/320-pixel-wide processed image will
look softer than the native image; increase that setting to compare quality.

The upper-right counter distinguishes actual displayed frames from completed
Phoenix calls per second and reports full C frame-call milliseconds. Inference
runs on a worker so a slow CPU call does not block the menu. Backend/resolution
changes take effect after an in-flight call finishes; obsolete results are
discarded. Result age is measured from source capture, including inference time.

Each neural result retains its source frame. Matching off/on split comparisons
use the corresponding full-resolution source, rather than comparing different
moments. Two enabled sides share the single Vulkan session, so switching sides
resets history. For repeatable comparisons, freeze the source and disable history.
**Save image & measurements** writes a PNG to `Pictures/Phoenix Museum` on Android
10+, with a JSON receipt in the app's external files directory. Android 9 saves
the PNG in the app's external `Pictures` directory. The image contains the actual
GL comparison output; Android menu widgets are not included. Raw neural input,
output and motion receipts remain in the app's private `files/captures` folder.
When testing motion, enable visitors/camera animation and history. Motion is
sampled relative to the previous submitted inference frame, including bone and
object motion. The readback packs signed motion to 1/32 pixel in [-1024,1024)
at processing resolution; larger vectors clamp. No separate depth/validity guide
is provided to Phoenix in this app.

The mobile renderer uses OpenGL ES 3 with animated vertex skinning, normal maps,
shadow maps, screen-space contact occlusion/reflections and skin softening. The
seven names correspond to mobile cost/appearance choices; this is not a port of
the Windows DXR renderer or its exact lighting output. Asset texture derivatives
are capped at 1024 pixels for color and 512 for normal/roughness maps. All four
body mesh levels and the original room placements are retained.

Vulkan device support is checked before inference. Unsupported devices produce
an explicit message; selecting Vulkan never silently runs CPU inference. Basic
phone Vulkan support does not imply the current Phoenix compute requirements.

**Vulkan compatibility mode (64/128-thread GPUs)** is an optional menu switch.
It selects subgroup-independent workgroup shaders and integer half storage for
drivers such as the Adreno 735. Standard support and compatibility support are
queried separately; the switch defaults off. This is still GPU inference, with
the same model controls. Runtime receipts report the selected compatibility
path. Unsupported base compute features remain an error.
The compatibility shaders preserve half subnormals and fused rounding boundaries
in software on the GPU. This favors correctness and can be slow; the counter
reports actual completed Phoenix calls separately from the live scene's FPS.
The optimized path uses register tiles for matrices and parallel workgroup row
reductions. In a controlled 320×144 phone test, median warm frame calls improved
from 2.010 s to 1.247 s (1.61×), with CPU-identical output. This is still below
one completed model call per second. Frozen comparisons reuse scene targets
between captures; dragging or changing settings refreshes the scene.
See [measured performance](BENCHMARKS.md) for timings and limits.


See [build instructions](BUILD.md) and [measured performance](BENCHMARKS.md).
