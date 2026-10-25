# Native glTF viewer sources

The model decoder, asynchronous loader, math and OpenGL renderer in
`fltk/model/` are a pinned MIT-source snapshot of fsvr. `fsvr-model.json`
records the upstream commit and source hashes. Original SPDX notices and
namespaces are retained; only the vendored header include paths are adapted.
No sibling checkout is needed to build or run the viewer.

cgltf and stb_image are pinned in `gltf-dependencies.json`; their MIT/public-domain notices are embedded in their headers. `LICENSE.native-graphics`
retains the native graphics adapter's upstream attribution. Model UI and
integration code use qimgvr's GPL-3.0-or-later license.

`fltk/model/domed.*` and `window-stereo.h` adapt Comic Chat XR's MIT window
geometry monitor and asymmetric frustum/cylinder math. The read-only legacy
`/api/integrations/comic-chat` response contains all window quads, so it is
usable for qimgvr's own `org.qimgvr.Sbs` window without a compositor update.
The client validates the launching compositor and selects only its own PID and
window identity. It never changes the compositor's scene or presentation mode.
