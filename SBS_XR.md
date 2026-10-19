# SBS images in an active XR session

This local branch is based on easymodo/qimgv at
`3127a2d211b124ad4fcf853d01e6df9323bdfdc3` and remains GPLv3.
The separate Arc SBS image worker uses the official pinned Depth Anything V2
Small model with Intel A770 OpenVINO/OpenCL acceleration.

Install Arc SBS 0.3.0 first. The default worker Python is
`~/.local/opt/arc-sbs/current/venv/bin/python`; override with
`QIMGV_SBS_PYTHON=/absolute/path/to/python`. Models and OpenVINO caches follow
the Arc SBS user-data paths. No proprietary VisionDepth3D code is used.

```bash
qimgv-sbs /path/to/image.jpg       # automatic XR output when connected
qimgv-sbs --sbs /path/to/image.jpg # also show local SBS if disconnected
qimgv-sbs --no-sbs image.jpg       # ordinary qimgv behavior
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
qimgv-sbs --sbs-present /path/to/full-sbs.png
```

This internal presentation mode watches atomically replaced files and uses a
fullscreen squeezed SBS canvas. It does not start an XR session by itself.

Build with Qt6 Widgets/Svg/PrintSupport/OpenGLWidgets, LinguistTools and Exiv2
development packages:

```bash
cmake -S . -B build -DUSE_QT5=OFF -DVIDEO_SUPPORT=OFF \
  -DOPENCV_SUPPORT=OFF -DSBS_TESTS=ON -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local/opt/qimgv-sbs"
cmake --build build --parallel 8
ctest --test-dir build --output-on-failure
cmake --install build
```

This local build disables optional mpv video and OpenCV scaling support because
their development packages are absent. The installed system qimgv is retained.
See the companion Arc SBS repository's `integrations/qimgv` for the pinned patch
and reproducible installer.
