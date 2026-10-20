# qimgvr

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

## Build and install locally

All upstream optional features are enabled in the local installation:

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

```bash
python3 scripts/install-local.py
```

The helper builds Release/Qt6 with `EXIV2`, `VIDEO_SUPPORT`, `OPENCV_SUPPORT`,
`KDE_SUPPORT`, and `SBS_TESTS` enabled, runs native tests, and installs into
`~/.local/opt/qimgvr`. It creates `qimgvr`, `qimgv`, and `qimgv-sbs` launchers in
`~/.local/bin`, a qimgvr desktop entry, and compatibility desktop entries. It
updates image associations and backs up existing launchers, settings and MIME
configuration in `~/.local/state/qimgvr/backups`. Existing qimgv settings remain
at `~/.config/qimgv`; qimgvr uses its own thumbnail/cache directory.

SBS inference requires [Arc SBS](https://github.com/jebot-git/arc-sbs) >=0.4.1,
the separately prepared pinned Depth Anything V2 Small model, and A770
OpenVINO/OpenCL drivers. Default worker interpreter:
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

## System installation

Configure the prefix before building: plugin and translation paths are compiled in.
With the development dependencies above installed:

```sh
cmake -S . -B build-system -DCMAKE_BUILD_TYPE=Release \
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

Basic manuals: [qimgvr.1](docs/man/qimgvr.1). After installation, use `man qimgvr`.

Part of the [de-xr desktop](https://github.com/topics/de-xr):
[DomeD](https://github.com/jebot-git/DomeD) provides the desktop/session,
[Ghostty XR](https://github.com/jebot-git/ghostty-xr) the terminal,
[mpvr](https://github.com/jebot-git/mpvr) video playback,
[fsvr](https://github.com/jebot-git/fsv-xr) file browsing,
[qimgvr](https://github.com/jebot-git/qimgvr) image viewing,
[Arc SBS](https://github.com/jebot-git/arc-sbs) depth conversion, and
[LightXR](https://github.com/jebot-git/LightXR) the optional login manager.
Each application has its own dependencies and can be used independently where
its selected backend permits.
