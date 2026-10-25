# Stereo glTF export and native viewing

The FLTK frontend supports stereo export and native glTF viewing. The tested
prototype has been accepted for installation; reconstruction remains an
approximate textured relief. Qt6 remains an explicit opt-in and has no new
exporter or native model viewer in this change.

Open an image with qimgvr, wait for its SBS rendering to finish,
and choose **File → Export as glTF...** in DomeD's universal menu. The stereo
presenter has the same action, including when it is a separate process. Export
uses that window's current normalized left/right still image. Navigation, edits,
layout changes and unfinished image loading invalidate the main viewer's prior
export until the new conversion is ready. Reconstruction and encoding run in a
background job after taking a snapshot of the selected image.

Choose `.glb` for a binary glTF or `.gltf` for JSON with an embedded binary data
URI. Both are self-contained; textures need no accompanying files. The save
dialog confirms replacements and the writer uses a unique temporary file before
an atomic rename. Presenter/temporary inputs default to the user's Pictures
folder or home directory, rather than their disposable working directory.

The implementation follows Comic Chat XR's spatial export approach: textured
triangle geometry, an embedded PNG, unlit materials, explicit normals/UVs and
aligned glTF 2.0 buffers. This is an independent implementation for qimgvr.

## Reconstruction and limits

OpenCV StereoSGBM estimates signed disparity from a bounded matching image
(maximum edge 1024). Alpha coverage, local texture and left/right correspondence consistency
reject unreliable matches. Both matching directions use padded search borders
so the matcher does not discard wide edge strips. Missing samples are extended
from reliable depth along image-edge-aware paths and smoothed within the same
disparity band; they are not converted into flat-plane islands. Positive crossed disparity comes forward; negative
disparity recedes. The left-eye texture retains its original resolution and RGBA
values. Matching does not re-run the Arc SBS model or change its GPU renderer.

The mesh uses an aspect-preserving grid with a maximum edge of 512 vertices. It assumes a 1.3 m image plane viewed
at 1.25 m with a 64 mm eye separation. These are export assumptions, not recovered
camera calibration; they are recorded in the node extras along with the match
fraction. The result is an approximate relief, not a complete reconstructed
scene or an exact replacement for the two eye views. Only the left-eye image
textures the relief; surfaces hidden from that image cannot be recovered.

Unmatched pixels inherit nearby reliable depth without crossing transparent
barriers. This is conservative depth completion, not recovered hidden scenery.
Regions with no connected reliable evidence remain on the convergence plane.
Triangles spanning a genuine large depth discontinuity are still omitted so
object boundaries do not become stretched curtains. Newly exposed surfaces
hidden from the left image can still contain gaps during large head movements.
Opaque images use opaque materials so viewers retain depth writes and correct
surface occlusion; binary alpha uses masking and partial alpha uses blending. If too few matches exist, the exporter explicitly reports
a flat fallback. A fully transparent image produces an error. Video frames and
unconverted 2D images are not offered by the menu.

An optional headless diagnostic exports an already-rendered stereo pair:

```sh
/opt/fltk-vr/bin/qimgvr --export-gltf scene.glb \
  --input-layout=full-sbs rendered-stereo.png
```

Use `half-sbs` for horizontally compressed packed eyes. The command requires an
explicit layout and does not infer depth from a single 2D source image.

## Verification

`scripts/test-gltf-export.py` exercises the actual executable with synthetic
signed stereo pairs, validates positions, normals and index ranges, checks exact
embedded RGBA texture pixels, half-SBS eye boundaries, flat fallback, input
errors and preservation of existing files. `scripts/test-gltf-ui.py` uses an
isolated FLTK session and deterministic worker to test both universal-menu
exports and generation invalidation through real native save dialogs.

`scripts/test-gltf-parallax.py` guards low-texture depth completion, opaque
surface occlusion, full coverage on continuous surfaces, and signed depth
boundaries. It fails on the initial exporter: a flat textured plane with a
low-texture patch lost all patch depth, while a sloped surface had a 95th-percentile
disparity error of 6.31 pixels. The corrected exporter measures 0.063 and 0.293
pixels on those fixtures, respectively, with full continuous-surface coverage.
These are controlled stereo tests, not a visual-quality claim for every input.

Khronos glTF Validator checks the generated GLB/JSON files. An independent fsvr
loader/renderer also loads and renders the generated geometry. Neither test
claims live-headset visual quality; that remains the prototype acceptance step.

References: [glTF 2.0](https://registry.khronos.org/glTF/),
[unlit materials](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_unlit),
[OpenCV StereoSGBM](https://docs.opencv.org/4.x/d2/d85/classcv_1_1StereoSGBM.html).

## XR chooser follow-up

Universal-menu actions are queued onto FLTK's event loop and acknowledged to the
menu host before invoking a modal chooser. Open, image-save and glTF-save chooser
objects are retained across uses so delayed Wayland input cannot target widgets
freed immediately after the dialog closes. The initial live prototype produced a
FLTK Wayland key-repeat crash after closing its stack-allocated chooser.

`scripts/test-xr-file-chooser.py` checks a real native Wayland chooser on an
isolated DomeD XR seat: immediate menu acknowledgement while the chooser stays
open, keyboard typing, a physical pointer ray hitting the filename field,
subsequent keyboard input, and holding Escape through dismissal without killing
the application. The live input failure still requires user confirmation after
the updated prototype is reopened.

## Mono file dialogs

The application sets an explicit `qimgvr` default window identity before creating
any windows. Only the SBS image presenter opts into `org.qimgvr.Sbs`; native
export/open/save and confirmation dialogs retain the mono identity. This avoids
FLTK inheriting the first presenter's SBS identity into a later chooser.

Run `scripts/test-xr-file-chooser.py` with `QIMGVR_CHOOSER_EXPORT=1` to verify
that the private DomeD scene reports an SBS presenter and a mono export chooser,
then exercise keyboard input, physical pointer selection and held-Escape close.
The default test mode checks the ordinary Open chooser.

## Native glTF viewing

Open a `.glb` or `.gltf` using the normal **File → Open...** chooser, a dropped
file URI, or a command-line filename. Models share the existing folder navigation
and thumbnail sidebar with images and videos; model rows use a filename
placeholder. Image editing, image saving and stereo-image export are disabled
for models. Switching back to an image or video restores its normal controls.

The viewer vendors fsvr's MIT glTF loader, resource-bounded decoder, material /
texture renderer, animation evaluator and asynchronous file watcher. Embedded,
data-URI and relative external buffers/images work; PNG and JPEG textures,
unlit/PBR materials, skinning/morph animations and the decoder's supported
extensions retain the fsvr behavior. Unsupported required extensions and malformed
assets report errors. Relative parent resources remain valid; remote/absolute
resource URIs are not fetched. A model load never enters the Arc SBS image
worker. No sibling checkout is needed at build or runtime; source pins and
licenses are in `third_party/`.

Drag with the left mouse button to orbit, Shift-drag or middle-drag to pan, and
use the wheel to zoom. **R**, **View → Fit**, or **Model → Reset model camera**
resets the view. Space / **Model → Play or pause animation** controls animation;
**Next animation** cycles clips and the seek slider scrubs the selected clip.
**Orbit automatically** is optional and starts disabled. Model details and
loader warnings are available through **File → Metadata**.

A qimgvr stereo-relief export's recorded reference camera is used initially,
preserving its source composition. **Model → Fit model bounds** selects the
generic model inspection camera; **Reference image view** restores the export
camera. Ordinary glTF assets without that metadata start with bounds fitting.
These camera choices do not repair geometry or invent missing background.

The normal viewer shows a mono 3D viewport. **Stereo → Auto when headset
connected** additionally opens a native geometry stereo window during DomeD XR
presentation. **Always (XR or preview)** enables it explicitly, including a
side-by-side desktop preview. **Off** / **Close stereo view** / Escape hides
that presenter. This renders the model directly from separate eye cameras; it
never estimates another image depth map or starts a separate OpenXR session.
DomeD's existing tracked-eye/window geometry is used for asymmetric projection
and per-eye cylinder reprojection, with a 64 mm fallback baseline. File dialogs
keep the mono identity. The monitor polls outside the UI/render thread; headset
late latching is outside this prototype.

Native UI and private Wayland/XR regression checks:

```sh
QIMGVR_BINARY=/absolute/path/to/prototype/qimgvr \
QIMGVR_MODEL_ARTIFACTS=/tmp/qimgvr-model-tests \
uv run --no-project --with numpy --with dbus-next --with pillow --with python-xlib \
  python scripts/test-gltf-viewer.py

QIMGVR_BINARY=/absolute/path/to/prototype/qimgvr \
QIMGVR_MODEL_FIXTURE=/absolute/path/to/stereo-relief.glb \
QIMGVR_MODEL_ARTIFACTS=/tmp/qimgvr-model-xr-tests \
uv run --no-project --with numpy --with pillow --with python-xlib \
  python scripts/test-gltf-xr.py
```

The first checks textured GLB/JSON assets, external resources, animation,
orbit/pan/zoom/reset, mixed-media navigation, reference versus bounds cameras,
SBS geometry frames and failures. The second starts a private nested DomeD,
injects head/eye poses and verifies actual differing eye images, head parallax,
curved-window geometry, and a mono chooser from the SBS model window. These
checks do not change the active desktop or establish live-headset comfort.
