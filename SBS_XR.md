# SBS images in an active XR session

qimgvr is based on easymodo/qimgv at
`3127a2d211b124ad4fcf853d01e6df9323bdfdc3` and remains GPLv3.
The separate Arc SBS image worker uses the official pinned Depth Anything V2
Small model with Intel A770 OpenVINO/OpenCL acceleration.

Install Arc SBS 0.4.0 first. The default worker Python is
`~/.local/opt/arc-sbs/current/venv/bin/python`; override with
`QIMGV_SBS_PYTHON=/absolute/path/to/python`. Models and OpenVINO caches follow
the Arc SBS user-data paths. No proprietary VisionDepth3D code is used.

```bash
qimgvr /path/to/image.jpg       # automatic XR output when connected
qimgvr --sbs /path/to/image.jpg # also show local SBS if disconnected
qimgvr --no-sbs image.jpg       # ordinary qimgv behavior
```

F8 or **Toggle SBS / XR output** in the context menu toggles conversion. In
automatic mode, a connected headset makes the initial toggle disable output;
without a connected headset it enables the local SBS preview. Continue navigating
with qimgv's usual controls in its main window. Each still image is converted
asynchronously. Rapid navigation retains only the latest pending image; obsolete
results do not replace the displayed image. The worker keeps its model compiled
across images. Processing delay is expected, especially on the first image.

Automatic XR output currently supports **WiVRn + WayVR**, as installed on this
workstation. Both WiVRn `HeadsetConnected` and `SessionRunning` must be true. It
creates its own WayVR surface with `left_right`, `stereo_full_frame=false`, using
a squeezed canvas with separately fitted eyes and symmetric margins. It leaves
the runtime and other overlays running. The presenter updates the same surface
after subsequent conversions; disabling SBS hides it and closing qimgv terminates
its owned presenter. Connection state is checked every three seconds while idle.

Only static, SDR images are converted; animations/videos stay in the regular
viewer and hide the image overlay. qimgv supplies its oriented/edited pixels,
composited on black. Images are limited to a 3840-pixel longest edge and rounded
to even dimensions. Internal intermediate output is full SBS; the 2560×1440 XR
surface uses half-SBS transport. This prototype estimates hidden surfaces and
can show edge/fill artifacts. Actual through-headset perception needs user review.

Existing full-SBS images can be shown without depth conversion:

```bash
qimgvr --sbs-present /path/to/full-sbs.png
```

This internal presentation mode watches atomically replaced files and uses a
fullscreen squeezed SBS canvas. It does not start an XR session by itself.

Build with Qt6 Widgets/Svg/PrintSupport/OpenGLWidgets, LinguistTools and Exiv2
development packages:

```bash
cmake -S . -B build -DUSE_QT5=OFF -DVIDEO_SUPPORT=OFF \
  -DOPENCV_SUPPORT=OFF -DSBS_TESTS=ON -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local/opt/qimgvr"
cmake --build build --parallel 8
ctest --test-dir build --output-on-failure
cmake --install build
```

The qimgvr local installation enables mpv, OpenCV scaling, Exiv2 and KDE
integration. Use `python3 scripts/install-local.py` to build/install all features;
the image-only example above is useful for development without mpv/OpenCV.
See README.md for the full build, compatibility launchers and system replacement.

## Existing stereo images

The opener recognizes SBS/FSBS/FULL-SBS and HSBS/HALF-SBS filename tokens,
JPEG JPS descriptors and PNG sTER metadata. JPS/PNS extensions default to
right-eye-first (cross-eyed); LR/PARALLEL filename tokens select left-first.
Explicit metadata takes precedence over filename hints. Wide aspect ratios alone
are not treated as stereo. Half-SBS is expanded and right-first pairs are reordered
before presentation. Existing stereo skips model loading and depth inference.
Unsupported stereo arrangements report an error rather than synthesizing depth.

Press **F9** (or use the context menu) to cycle Auto, 2D, Full SBS and Half SBS.
The explicit selection overrides detection; use it for ambiguous/unmarked images:

```bash
qimgvr --input-layout full-sbs unmarked.png
qimgvr --input-layout 2d panorama.jpg
```

The extension convention follows [sView's stereo format documentation](https://github.com/gkv311/sview/blob/master/docs/stereoformats.md).

## Optional DomeD integration

Requires Arc SBS 0.4.0 and DomeD with the qimgvr integration endpoint and per-eye
composition support. Enable the default viewer only for the Dome desktop:

```bash
python3 scripts/install-domed.py
# Restore the previous Dome associations:
python3 scripts/install-domed.py --disable
```

The installer preserves other desktops' defaults and records previous Dome
associations under `~/.local/state/qimgvr`. Launch manually with
`qimgvr --domed image.jpg`. File URLs with escaped characters are supported.

When launched inside DomeD with an active XR renderer, qimgvr opens a separate
frameless SBS window on that compositor's XR socket. The normal viewer remains
on the desktop. DomeD submits each image half exclusively to its corresponding
OpenXR eye, for quad and cylinder layers. Navigation updates the owned window;
closing the viewer or disabling output closes it. An inactive XR session creates
no XR window. This mode does not start a headset session.

Discovery validates the current Wayland socket, same-user compositor PID and its
private session record before contacting the advertised numeric loopback port.
The integration is opt-in and uses only the current compositor session.
