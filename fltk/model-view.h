// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "packed-frame.h"
#include "model/domed.h"
#include "model/gltf-render.h"
#include "model/model-loader.h"
#include <FL/Fl_Gl_Window.H>
#include <functional>
namespace qimgvr {
struct ModelCamera {
  float yaw = 0, pitch = 0, distance = 1.5f, panX = 0, panY = 0;
  double seconds = 0;
  int animation = 0;
  bool playing = true, orbit = false, reference = true;
  void reset() {
    yaw = pitch = panX = panY = 0;
    distance = 1.5f;
    orbit = false;
    reference = true;
  }
};
class ModelView : public Fl_Gl_Window {
  struct Targets;
  std::unique_ptr<Targets> targets;
  fsv::gltf::Renderer renderer;
  std::shared_ptr<const fsv::gltf::Model> model;
  std::shared_ptr<ModelCamera> camera;
  std::string error;
  int downX = 0, downY = 0, button = 0;
  bool stereo = false;
  void drawStereo();
  void releaseGpu();
  PackedFrame monoFrame;

public:
  bool packed = false;
  void setStereo(bool value) { if (stereo != value) { stereo = value; redraw(); } }
  qimgvr_model::DomeMode presentation;
  size_t frames = 0, stereoFrames = 0;
  ModelView(int, int, int, int, std::shared_ptr<ModelCamera>,
            bool stereo = false);
  ~ModelView();
  void hide() override;
  void setModel(std::shared_ptr<const fsv::gltf::Model>);
  const std::shared_ptr<const fsv::gltf::Model> &asset() const { return model; }
  const std::string &failure() const { return error; }
  void reset();
  void draw() override;
  int handle(int) override;
  bool snapshot(const std::filesystem::path &);
};
} // namespace qimgvr
