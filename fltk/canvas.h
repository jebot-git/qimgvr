// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "appearance.h"
#include "image.h"
#include <FL/Fl_Gl_Window.H>
#include <FL/gl.h>
#include <atomic>
#include <functional>
#include <mpv/client.h>
#include <mpv/render_gl.h>
class Canvas : public Fl_Gl_Window {
  GLuint texture = 0;
  bool dirty = true;
  int dragX = 0, dragY = 0, cropX = 0, cropY = 0;
  bool draggingCrop = false;
  double x0 = 0, y0 = 0, drawW = 1, drawH = 1;
  mpv_handle *mpv = nullptr;
  mpv_render_context *render = nullptr;
  std::atomic_bool videoDirty{false};
  std::string pendingVideo;
  std::string videoError;
  void upload();
  void openVideo();
  void closeVideo();
  void drawImage(bool stereoEye = false);

public:
  cv::Mat pixels;
  double zoom = 1, panX = 0, panY = 0;
  bool fit = true, stereo = false, cropMode = false;
  std::function<void(cv::Rect)> cropped;
  std::function<void(std::string)> failed;
  std::function<void()> edited;
  Canvas(int x, int y, int w, int h);
  ~Canvas();
  void setImage(const cv::Mat &image, bool reset = true);
  void setVideo(std::string path);
  bool video() const { return mpv || !pendingVideo.empty(); }
  void poll();
  void command(const std::vector<std::string> &);
  double position(), duration();
  bool paused();
  void rotate(bool clockwise);
  void flip(bool horizontal);
  void resetView();
  void hide() override;
  void draw() override;
  int handle(int) override;
  bool snapshot(const std::filesystem::path &);
};
