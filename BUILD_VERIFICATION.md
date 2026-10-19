# Local build and replacement verification

Verified 2026-10-19 on Nobara 44 x86_64, GCC 16.2.1, Qt 6.11.2.

qimgvr preserves the qimgv history through upstream revision
`3127a2d211b124ad4fcf853d01e6df9323bdfdc3` and the local SBS implementation
commit `2eab8823`. It has its own repository and `main` branch.

Enabled Release features: `USE_MPV USE_EXIV2 USE_KDE_BLUR USE_OPENCV`.
Linked system runtimes: mpv 0.41.0, OpenCV 4.13.0 core/imgproc, Exiv2 0.28.9,
KF6WindowSystem 6.30.0 and Qt6. System Qt/KDE image-format plugins are discovered,
including AVIF, HEIF/HEIC, JPEG XL, SVG, WebP, TIFF, EXR, PSD and camera RAW.
`qimgvr --build-options` prints the complete detected format list.

Build headers were downloaded/extracted into a private user build sysroot because
development packages were initially absent and root authentication was not yet
available. The build sysroot adapts pkg-config metadata for dynamic mpv linking
and exposes only OpenCV core/imgproc imported targets against the installed
runtime libraries. It is not a runtime dependency or part of the repository.
Ordinary builds use system development packages listed in README.md.

Checks completed:

- Native SBS tests: independent eye placement/portrait margins, asynchronous
  navigation/stale results, and atomic image reload/owner cleanup.
- Native mpv test: generated H.264 video, actual plugin factory loading,
  playback position advancing and colored pixels captured from its OpenGL widget.
  An earlier interactive desktop run stalled; subsequent isolated playback passed.
  The final test ignores mouse input/focus to avoid pausing on user clicks.
- Installed `qimgv --no-sbs source.mp4` mapped this fork's own
  `~/.local/opt/qimgvr/lib/qimgvr/player_mpv.so`, system OpenCV and KF6 libraries.
  It did not use the old system qimgv plugin. Dynamic dependency checks found
  no missing libraries.
- Installed automatic image conversion reached the connected Quest Pro's live
  WayVR session after the system RPM was removed. The dedicated overlay was
  `left_right`, `stereo_full_frame=false`. The main automation window was
  offscreen; the actual XR presenter used Wayland. Through-headset perception
  was not assessed.
- Fixed Qt Wayland's application-name suffix on presenter titles so the worker
  can find the exact overlay. Other XR applications remain running.
- `sudo dnf5 remove --no-autoremove --assumeyes qimgv` completed successfully in
  an interactive sudo terminal. `rpm -q qimgv` confirms it is absent, and
  `/usr/bin/qimgv` has been removed. Shared runtime dependencies were retained.
- `qimgv`, `qimgvr`, and `qimgv-sbs` resolve to local qimgvr 1.2.1. JPEG and PNG
  defaults resolve to `qimgvr.desktop`. Compatibility desktop entries also launch
  qimgvr. Desktop entries pass validation. Existing settings remain compatible.

Installation prefix: `~/.local/opt/qimgvr`. Source: `/home/blux/qimgvr`.
Prior launchers/MIME settings are backed up under
`~/.local/state/qimgvr/backups/20261019-222524-660074`.
Machine-local validation media and logs are outside the repository under
`~/.cache/qimgvr-validation` and `~/.cache/qimgv-build`.

Reopen already-running viewer windows to load the completed fork build.

Version 1.2.0: both native test targets passed with all optional features enabled.
DomeD's isolated integration test exercised the real default MIME opener, Unicode
file URLs, distinct per-eye image pixels, XR-only window placement, owner cleanup,
inactive-session handling and ordinary A770 depth conversion. An existing SBS
image succeeded with a deliberately missing model. The test used a simulated XR
heartbeat; actual DomeD through-headset perception has not been verified.

Transparency update (2026-10-20, qimgvr 1.2.1 / Arc SBS 0.4.1): native SBS and
mpv playback targets passed. Qt tests verify premultiplied source snapshot
coverage, separate per-eye partial alpha, transparent margins and clearing old
pixels. Arc's expanded CPU/A770 suite and DomeD integration verify RGBA conversion
and existing-SBS bypass. The compositor exported 50% red as `[128,0,0,128]` and
25% blue as `[0,0,64,64]`, with alpha-zero margins. No DomeD binary change was
needed; its existing premultiplied OpenXR composition path retains this alpha.
