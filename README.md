# qimgvr

Image/video viewer for de-xr, forked from [qimgv](https://github.com/easymodo/qimgv).
**CMake defaults to FLTK 1.4.5 and OpenGL.** The retained Qt frontend is selected
explicitly with `-DDE_XR_TOOLKIT=Qt6`. Upstream history and GPLv3 notices remain;
see [README_UPSTREAM.md](README_UPSTREAM.md). The two frontends have different
shortcuts, settings, command-line flags and remaining limitations.

## Default FLTK build

Requires C++17, CMake, the de-xr patched **FLTK 1.4.5** shared libraries,
OpenGL/EGL, OpenCV core/imgcodecs/imgproc/calib3d, Exiv2, libmpv, Fontconfig, GIO,
Wayland/X11 development libraries and `wayland-scanner`. Native bitmap-font,
Wayland app-ID and popup/dialog patches are required; see
[FLTK notes](FLTK_EXPERIMENT.md). Use the matching runtime when launching.

```sh
FLTK_PREFIX=/path/to/patched-fltk-1.4.5
cmake -S . -B build-fltk -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$FLTK_PREFIX" -DDE_XR_TOOLKIT=FLTK
cmake --build build-fltk --parallel
ctest --test-dir build-fltk --output-on-failure
LD_LIBRARY_PATH="$FLTK_PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
  ./build-fltk/fltk/qimgvr --sbs=off /path/to/photo.jpg
DESTDIR="$PWD/stage" cmake --install build-fltk --prefix /usr/local
install -Dm644 docs/man/qimgvr.1 stage/usr/local/share/man/man1/qimgvr.1
```

Build/stage as your regular user and review artifacts before privileged system
installation. The FLTK target installs its executable; the Qt compatibility
launchers, desktop/MIME setup, plugins and manual installation are separate.
The source Qt `install-local.py` helper does not currently select Qt in CMake,
so use the explicit Qt configuration below rather than treating it as the FLTK
installer. Staging the manual explicitly works for the FLTK target.

## FLTK use and controls

```sh
qimgvr /path/to/photo.jpg                  # saved stereo mode; auto on first launch
qimgvr --sbs=always /path/to/photo.jpg      # SBS directly in the main view
qimgvr --no-sbs /path/to/video.mp4
qimgvr --input-layout=half-sbs stereo.jpg
qimgvr --build-options
```

`--sbs` alone is equivalent to `--sbs=always`; accepted modes are `auto`,
`always` and `off`. Input layout accepts `auto`, `2d`, `full-sbs` and `half-sbs`.
`--version` and `--build-options` are safe non-window introspection commands.
Use `--help` for the FLTK options. It has no thumbnail-generation CLI. Its
`--smoke-test OUTPUT.png` captures a diagnostic image. Internal presenter flags
are worker transport details, not normal DomeD launch options.

Native decoding provides alpha/orientation/metadata, edit/undo/save, thumbnails,
slideshow and embedded libmpv playback. Use Ctrl+O to open, Ctrl+Shift+S to save
as, Ctrl+Z to undo, arrows for previous/next, F to fit, 1 for actual pixels,
T for thumbnails and F11 for fullscreen. E/Q rotate, H/V flip and C crops a
selection. Space pauses video, Alt+Left/Right seek, M mutes and +/- changes
volume. **Stereo → Always (XR or desktop)** uses S; F8/F9 belong to the Qt
frontend, not this FLTK build.

The Arc SBS worker performs still-image inference in a separate persistent
process; already-packed SBS bypasses depth inference. `QIMGV_SBS_PYTHON` selects
its interpreter (default `/opt/arc-sbs/venv/bin/python` if present, otherwise
`python3`). It needs `arc_sbs`, the separately prepared model and appropriate GPU
drivers. `QIMGVR_SBS_DEVICE` and `QIMGVR_SBS_RENDERER` override worker settings.
Normal viewing does not require the depth model. Converted SBS images and glTF
stereo render in the main viewer; no preview window is created. S toggles
stereo, Escape disables it, and F11 uses the existing main-window fullscreen.
The selected mode is saved. File/export dialogs remain mono. Inside DomeD,
turning stereo off supplies the same mono view to both eyes; no second XR
session is created. Thumbnails use a horizontal strip in SBS mode.

The FLTK frontend supports **Export as glTF...** for rendered stereo stills,
plus native `.glb` / `.gltf` viewing with model controls, animation and tracked
stereo presentation. Reconstruction is an approximate textured relief, not a
complete recovered scene. See [usage, limits and validation](docs/GLTF_EXPORT_PROTOTYPE.md).

FLTK exports native command menus where supported, uses native FLTK file
choosers and follows DomeD's palette/bitmap font live. Its settings are in
`$XDG_CONFIG_HOME/qimgvr/fltk.json`; they do not replace the Qt `qimgv` settings.
Full RAW/animation/ICC/printing/high-quality resampling parity is incomplete.
Hardware depth acceleration and physical headset/WiVRn streaming still need
validation on the target setup; simulated HMD tests do not establish that support.

## Opt-in Qt frontend

**The following describes the retained Qt frontend only.** Configure a separate
build with `-DDE_XR_TOOLKIT=Qt6`. [SBS_XR.md](SBS_XR.md) documents its stereo
controls; those Qt-specific shortcuts/options do not all apply to FLTK.

qimgvr exports native **File**, **Edit**, **View**, **Go**, and **Window** menus
to DomeD's universal menu bar through Qt's DBusMenu/AppMenu integration on
Wayland. Commands use the existing action manager and customized shortcuts;
image/edit availability, slideshow, shuffle, and fullscreen state update live.
The View menu includes SBS/XR output and input-layout commands. DomeD adds its
desktop/XR window controls. The custom context menu remains available when no
global menu host is present; no platform-theme override is required.

Build the menu tests with `-DMENU_TESTS=ON`, then run
`ctest --test-dir build -R applicationmenu --output-on-failure`.

Qt6 image viewer with Intel Arc accelerated 2D-to-SBS conversion and automatic
output in an active WiVRn/WayVR or DomeD XR session. Independent GPLv3 fork of
[easymodo/qimgv](https://github.com/easymodo/qimgv), preserving its history and
the upstream `3127a2d211b124ad4fcf853d01e6df9323bdfdc3` base. Original qimgv
features, authors and screenshots are documented in [README_UPSTREAM.md](README_UPSTREAM.md).

```bash
qimgvr /path/to/photo.jpg
qimgv /path/to/photo.jpg          # compatibility command after local install
qimgvr --sbs /path/to/photo.jpg   # local SBS preview when XR is disconnected
qimgvr --no-sbs /path/to/video.mp4
```

Still photos convert in a persistent background process. Browse normally while
the latest image is processed; the result updates a dedicated SBS overlay in the
connected WiVRn/WayVR session. F8 or **Toggle SBS / XR output** toggles output.
Images use qimgv's oriented/edited pixels with transparency preserved; animations and videos use the ordinary
viewer. See [SBS_XR.md](SBS_XR.md) for stereo controls and limitations.

Optional DomeD output, existing SBS detection and F9 input overrides are described
in [SBS_XR.md](SBS_XR.md). Enable the Dome-only default opener with
`python3 scripts/install-domed.py`.

## Qt build and local installation

The retained Qt backend has these optional features (enable them explicitly):

- mpv video playback through the separately built `player_mpv.so` plugin.
- OpenCV high-quality scaling.
- Exiv2 metadata support.
- KDE WindowSystem integration and blur.
- Qt6 image plugins supplied by the system, including AVIF/HEIF, JPEG XL,
  SVG, WebP, TIFF, EXR, PSD and camera RAW formats where their codecs are present.

Dependencies: C++ compiler, CMake, Git, Qt6 base/Svg/tools development packages,
Qt Test, Exiv2 development files, OpenCV core/imgproc development files, libmpv
development files, and KF6WindowSystem development files. On Fedora/Nobara,
these include `qt6-qtbase-devel qt6-qtsvg-devel qt6-qttools-devel exiv2-devel
opencv-devel mpv-devel kf6-kwindowsystem-devel`. Install `qt6-qtimageformats`
and `kf6-kimageformats` for additional runtime image plugins.

```sh
cmake -S . -B build -DDE_XR_TOOLKIT=Qt6 -DUSE_QT5=OFF \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_LIBDIR=lib \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local/opt/qimgvr"
python3 scripts/install-local.py
```

The legacy helper does not select the new CMake toolkit itself, so the explicit
Qt cache configuration above is required before using it on this branch.
The helper then enables `EXIV2`, `VIDEO_SUPPORT`, `OPENCV_SUPPORT`,
`KDE_SUPPORT` and `SBS_TESTS`, runs native tests and installs into
`~/.local/opt/qimgvr`. It creates `qimgvr`, `qimgv`, and `qimgv-sbs` launchers in
`~/.local/bin`, a qimgvr desktop entry, and compatibility desktop entries. It
updates image associations and backs up existing launchers, settings and MIME
configuration in `~/.local/state/qimgvr/backups`. Existing qimgv settings remain
at `~/.config/qimgv`; qimgvr uses its own thumbnail/cache directory.

SBS inference requires [Arc SBS](https://github.com/jebot-git/arc-sbs) >=0.4.1,
the separately prepared pinned Depth Anything V2 Small model, and A770
OpenVINO/OpenCL drivers. Default worker interpreter:
`/opt/arc-sbs/venv/bin/python` for a system installation, then the legacy
`~/.local/opt/arc-sbs/current/venv/bin/python`; override with `QIMGV_SBS_PYTHON`.
Ordinary image/video viewing does not need the conversion model.

The fork can coexist with the distribution qimgv package. Review desktop/MIME
associations before choosing a default viewer. Check compiled features and detected
codecs with `qimgvr --build-options`.

The mpv plugin is loaded from qimgvr's own installation directory; it does not
fall back to `/usr/lib64/qimgv`. Native tests cover SBS geometry, asynchronous
navigation, file updates/cleanup, and actual mpv OpenGL playback. The playback
test needs FFmpeg and an active X11/Xwayland display; SBS tests run offscreen.

Upstream copyright notices and GPLv3 [LICENSE](LICENSE) are retained. The
converter is a separate process with its own model/dependency licenses.

## Qt system installation

Configure the prefix before building: plugin and translation paths are compiled in.
With the development dependencies above installed:

```sh
cmake -S . -B build-system -DDE_XR_TOOLKIT=Qt6 -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_INSTALL_LIBDIR=lib \
  -DUSE_QT5=OFF -DEXIV2=ON -DVIDEO_SUPPORT=ON \
  -DOPENCV_SUPPORT=ON -DKDE_SUPPORT=ON
cmake --build build-system
sudo cmake --install build-system
sudo update-desktop-database /usr/local/share/applications
```

For a shared, isolated Arc SBS runtime, set `QIMGV_SBS_PYTHON` to its interpreter,
for example `/usr/local/libexec/arc-sbs/venv/bin/python`. Ensure that runtime and
its prepared model are readable by the users who will run conversion. The
per-user helper above remains a separate installation choice.

## Command manuals and related projects

Manual: [qimgvr.1](docs/man/qimgvr.1). Once the manual is installed, use `man qimgvr`.

Part of the [de-xr desktop](https://github.com/topics/de-xr):
[DomeD](https://github.com/jebot-git/DomeD) provides the desktop/session,
[Ghostty XR](https://github.com/jebot-git/ghostty-xr) the terminal,
[mpvr](https://github.com/jebot-git/mpvr) video playback,
[cmuxr](https://github.com/jebot-git/cmuxr) music playback and an audio-reactive XR floor,
[fsvr](https://github.com/jebot-git/fsv-xr) file browsing,
[qimgvr](https://github.com/jebot-git/qimgvr) image viewing,
[Arc SBS](https://github.com/jebot-git/arc-sbs) depth conversion, and
[LightXR](https://github.com/jebot-git/LightXR) the optional login manager.
Each application has its own dependencies and can be used independently where
its selected backend permits.

### Qt DomeD appearance and bitmap text

Inside DomeD, qimgvr watches the session appearance file and applies its palette
and exact bitmap font family, style and native pixel size. Thumbnail labels,
folder navigation, menus and zoom labels refresh their fonts and cached metrics
on live changes. Session overrides do not overwrite the saved qimgvr theme.
Unavailable bitmap faces or strikes fall back to the Qt application font.
Other compositors continue using qimgvr's normal Qt/theme settings.
