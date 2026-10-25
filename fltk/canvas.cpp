// SPDX-License-Identifier: GPL-3.0-or-later
#define GL_GLEXT_PROTOTYPES
#include "canvas.h"
#include "appearance.h"
#include <EGL/egl.h>
#include <cmath>
#include <dlfcn.h>
#include <fstream>
#include <opencv2/imgproc.hpp>
Canvas::Canvas(int x, int y, int w, int h) : Fl_Gl_Window(x, y, w, h) {
  mode(FL_RGB | FL_ALPHA | FL_DOUBLE);
  end();
}
Canvas::~Canvas() {
  if (context())
    make_current();
  closeVideo();
  if (context()) {
    if (texture) glDeleteTextures(1, &texture);
    monoFrame.release();
  }
}
void Canvas::hide() {
  if (context()) {
    make_current();
    closeVideo();
    if (texture)
      glDeleteTextures(1, &texture);
    texture = 0;
    monoFrame.release();
    dirty = true;
    firstImageFrame = true;
  }
  Fl_Gl_Window::hide();
}
void Canvas::setImage(const cv::Mat &image, bool reset) {
  if (context())
    make_current();
  closeVideo();
  pendingVideo.clear();
  pixels = image;
  dirty = true;
  if (reset)
    resetView();
  redraw();
}
void Canvas::setVideo(std::string path) {
  if (context())
    make_current();
  closeVideo();
  pendingVideo = std::move(path);
  pixels.release();
  videoError.clear();
  redraw();
}
void Canvas::closeVideo() {
  if (render) {
    mpv_render_context_set_update_callback(render, nullptr, nullptr);
    mpv_render_context_free(render);
    render = nullptr;
  }
  if (mpv) {
    mpv_terminate_destroy(mpv);
    mpv = nullptr;
  }
}
void Canvas::openVideo() {
  if (pendingVideo.empty())
    return;
  auto path = std::move(pendingVideo);
  pendingVideo.clear();
  mpv = mpv_create();
  if (!mpv)
    throw std::runtime_error("Cannot create media player");
  // Use the same verified BDF face and effective strike as the FLTK widgets.
  auto selected = bdf_fonts::qt_alias(dx::selected_font, "media");
  bdf_fonts::register_font(selected.file);
  auto family = selected.family;
  auto pixels = std::to_string(dx::size);
  mpv_set_option_string(mpv, "osd-font", family.c_str());
  mpv_set_option_string(mpv, "sub-font", family.c_str());
  mpv_set_option_string(mpv, "osd-font-size", pixels.c_str());
  mpv_set_option_string(mpv, "sub-font-size", pixels.c_str());
  mpv_set_option_string(mpv, "vo", "libmpv");
  mpv_set_option_string(mpv, "hwdec", "auto-safe");
  mpv_set_option_string(mpv, "keep-open", "yes");
  mpv_set_option_string(mpv, "terminal", "no");
  if (auto ao = getenv("QIMGVR_TEST_AO"))
    mpv_set_option_string(mpv, "ao", ao);
  int error = mpv_initialize(mpv);
  if (error < 0)
    throw std::runtime_error(mpv_error_string(error));
  mpv_opengl_init_params init{};
  init.get_proc_address = [](void *, const char *name) -> void * {
    auto value = dlsym(RTLD_DEFAULT, name);
    return value ? value : (void *)eglGetProcAddress(name);
  };
  mpv_render_param params[] = {
      {MPV_RENDER_PARAM_API_TYPE, (void *)MPV_RENDER_API_TYPE_OPENGL},
      {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &init},
      {MPV_RENDER_PARAM_INVALID, nullptr}};
  error = mpv_render_context_create(&render, mpv, params);
  if (error < 0)
    throw std::runtime_error(
        std::string("Cannot create OpenGL video renderer: ") +
        mpv_error_string(error));
  mpv_render_context_set_update_callback(
      render,
      [](void *p) {
        ((Canvas *)p)->videoDirty = true;
        Fl::awake();
      },
      this);
  command({"loadfile", path});
}
void Canvas::command(const std::vector<std::string> &args) {
  if (!mpv)
    return;
  std::vector<const char *> pointers;
  for (auto &s : args)
    pointers.push_back(s.c_str());
  pointers.push_back(nullptr);
  int error = mpv_command_async(mpv, 0, pointers.data());
  if (error < 0 && failed)
    failed(mpv_error_string(error));
}
void Canvas::poll() {
  if (videoDirty.exchange(false))
    redraw();
  if (!mpv)
    return;
  if (fontRevision != dx::font_revision) {
    fontRevision = dx::font_revision;
    auto selected = bdf_fonts::qt_alias(dx::selected_font, "media");
    bdf_fonts::register_font(selected.file);
    auto family = selected.family;
    auto pixels = std::to_string(dx::size);
    mpv_set_property_string(mpv, "osd-font", family.c_str());
    mpv_set_property_string(mpv, "sub-font", family.c_str());
    mpv_set_property_string(mpv, "osd-font-size", pixels.c_str());
    mpv_set_property_string(mpv, "sub-font-size", pixels.c_str());
  }
  while (auto event = mpv_wait_event(mpv, 0)) {
    if (event->event_id == MPV_EVENT_NONE)
      break;
    if (event->event_id == MPV_EVENT_END_FILE) {
      auto end = (mpv_event_end_file *)event->data;
      if (end && end->error < 0 && failed)
        failed(std::string("Playback: ") + mpv_error_string(end->error));
    }
  }
}
double Canvas::position() {
  double value = 0;
  if (mpv)
    mpv_get_property(mpv, "time-pos", MPV_FORMAT_DOUBLE, &value);
  return value;
}
double Canvas::duration() {
  double value = 0;
  if (mpv)
    mpv_get_property(mpv, "duration", MPV_FORMAT_DOUBLE, &value);
  return value;
}
bool Canvas::paused() {
  int value = 0;
  if (mpv)
    mpv_get_property(mpv, "pause", MPV_FORMAT_FLAG, &value);
  return value;
}
void Canvas::resetView() {
  fit = true;
  zoom = 1;
  panX = panY = 0;
  redraw();
}
void Canvas::rotate(bool clockwise) {
  if (pixels.empty())
    return;
  cv::Mat next;
  cv::rotate(pixels, next,
             clockwise ? cv::ROTATE_90_CLOCKWISE
                       : cv::ROTATE_90_COUNTERCLOCKWISE);
  setImage(next);
  if (edited)
    edited();
}
void Canvas::flip(bool horizontal) {
  if (pixels.empty())
    return;
  cv::Mat next;
  cv::flip(pixels, next, horizontal ? 1 : 0);
  setImage(next, false);
  if (edited)
    edited();
}
void Canvas::upload() {
  if (texture)
    glDeleteTextures(1, &texture);
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  GLint maximum = 0;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);
  cv::Mat source = pixels;
  if (source.cols > maximum || source.rows > maximum) {
    double scale =
        std::min(double(maximum) / source.cols, double(maximum) / source.rows);
    cv::resize(source, source, {}, scale, scale, cv::INTER_AREA);
  }
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, source.cols, source.rows, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, source.data);
  dirty = false;
}
void Canvas::drawImage(bool) {
  double W = w(), H = h();
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0, W, H, 0, -1, 1);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_LIGHTING);
  glDisable(GL_TEXTURE_2D);
  glDisable(GL_BLEND);
  if (!stereo)
    for (int y = 0; y < h(); y += 16)
      for (int x = 0; x < w(); x += 16) {
        double shade = ((x / 16 + y / 16) % 2) ? .14 : .19;
        glColor3d(shade, shade, shade);
        glBegin(GL_QUADS);
        glVertex2d(x, y);
        glVertex2d(x + 16, y);
        glVertex2d(x + 16, y + 16);
        glVertex2d(x, y + 16);
        glEnd();
      }
  if (pixels.empty())
    return;
  if (dirty || !texture)
    upload();
  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, texture);
  glColor4d(1, 1, 1, 1);
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                      GL_ONE_MINUS_SRC_ALPHA);
  auto quad = [&](double x, double y, double width, double height, double u1,
                  double u2) {
    glBegin(GL_QUADS);
    glTexCoord2d(u1, 0);
    glVertex2d(x, y);
    glTexCoord2d(u2, 0);
    glVertex2d(x + width, y);
    glTexCoord2d(u2, 1);
    glVertex2d(x + width, y + height);
    glTexCoord2d(u1, 1);
    glVertex2d(x, y + height);
    glEnd();
  };
  if (stereo) {
    double eye = pixels.cols / 2.;
    double scale = fit ? std::min(W / eye, H / pixels.rows) : zoom,
           width = eye * scale, height = pixels.rows * scale;
    glEnable(GL_SCISSOR_TEST);
    for (int i = 0; i < 2; ++i) {
      int left = i * pixel_w() / 2, right = (i + 1) * pixel_w() / 2;
      glScissor(left, 0, right - left, pixel_h());
      quad(i * W / 2 + (W - width) / 4 + panX / 2, (H - height) / 2 + panY,
           width / 2, height, i * .5, (i + 1) * .5);
    }
    glDisable(GL_SCISSOR_TEST);
  } else {
    double scale = fit ? std::min(W / pixels.cols, H / pixels.rows) : zoom;
    drawW = pixels.cols * scale;
    drawH = pixels.rows * scale;
    x0 = (W - drawW) / 2 + panX;
    y0 = (H - drawH) / 2 + panY;
    quad(x0, y0, drawW, drawH, 0, 1);
  }
  glDisable(GL_BLEND);
  glDisable(GL_TEXTURE_2D);
  if (draggingCrop) {
    glColor3d(1, .8, .2);
    glLineWidth(2);
    glBegin(GL_LINE_LOOP);
    glVertex2d(cropX, cropY);
    glVertex2d(dragX, cropY);
    glVertex2d(dragX, dragY);
    glVertex2d(cropX, dragY);
    glEnd();
  }
}
void Canvas::draw() {
  if (!context_valid()) {
    texture = 0;
    monoFrame.abandon();
    dirty = true;
    firstImageFrame = true;
  }
  glViewport(0, 0, pixel_w(), pixel_h());
  glClearColor(0, 0, 0, stereo ? 0 : 1);
  glClear(GL_COLOR_BUFFER_BIT);
  try {
    if (!pendingVideo.empty())
      openVideo();
    if (render) {
      mpv_opengl_fbo fbo{0, pixel_w(), pixel_h(), 0};
      int flip = 1, block = 0;
      mpv_render_param params[] = {
          {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
          {MPV_RENDER_PARAM_FLIP_Y, &flip},
          {MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &block},
          {MPV_RENDER_PARAM_INVALID, nullptr}};
      mpv_render_context_render(render, params);
      if (packed) monoFrame.pack(pixel_w(), pixel_h());
    } else {
      drawImage();
      if (packed && !stereo) monoFrame.pack(pixel_w(), pixel_h());
      if (firstImageFrame && !pixels.empty()) {
        firstImageFrame = false;
        // A Wayland GL child can submit its first buffer after the parent has
        // finished painting. Commit the parent again to publish the child and
        // its stacking/position state, even when the image is otherwise idle.
        if (auto owner = window()) owner->redraw();
      }
    }
  } catch (const std::exception &e) {
    videoError = e.what();
    closeVideo();
    if (failed)
      failed(videoError);
  }
  glFlush();
}
int Canvas::handle(int event) {
  if (event == FL_FOCUS || event == FL_UNFOCUS)
    return 1;
  if (event == FL_PUSH) {
    take_focus();
    dragX = Fl::event_x() - x();
    dragY = Fl::event_y() - y();
    if (cropMode && !pixels.empty()) {
      cropX = dragX;
      cropY = dragY;
      draggingCrop = true;
    }
    return 1;
  }
  if (event == FL_DRAG) {
    int X = Fl::event_x() - x(), Y = Fl::event_y() - y();
    if (!draggingCrop) {
      panX += (X - dragX) * (stereo && !xrCoordinates ? 2 : 1);
      panY += Y - dragY;
    }
    dragX = X;
    dragY = Y;
    redraw();
    return 1;
  }
  if (event == FL_RELEASE && draggingCrop) {
    draggingCrop = false;
    cropMode = false;
    int left =
            std::clamp(int((std::min(cropX, dragX) - x0) / drawW * pixels.cols),
                       0, pixels.cols),
        top =
            std::clamp(int((std::min(cropY, dragY) - y0) / drawH * pixels.rows),
                       0, pixels.rows),
        right =
            std::clamp(int((std::max(cropX, dragX) - x0) / drawW * pixels.cols),
                       0, pixels.cols),
        bottom =
            std::clamp(int((std::max(cropY, dragY) - y0) / drawH * pixels.rows),
                       0, pixels.rows);
    if (right > left && bottom > top && cropped)
      cropped({left, top, right - left, bottom - top});
    redraw();
    return 1;
  }
  if (event == FL_MOUSEWHEEL && !pixels.empty()) {
    double old =
        fit ? std::min(double(w()) / (pixels.cols / (stereo ? 2. : 1.)),
                       double(h()) / pixels.rows)
            : zoom;
    double next = std::clamp(old * std::pow(1.15, -Fl::event_dy()), .01, 64.);
    double X = Fl::event_x() - x() - w() / 2.,
           Y = Fl::event_y() - y() - h() / 2.;
    if (stereo) {
      double local = Fl::event_x() - x();
      if (local >= w() / 2.)
        local -= w() / 2.;
      X = local * 2 - w() / 2.;
    }
    panX = X - (X - panX) * next / old;
    panY = Y - (Y - panY) * next / old;
    zoom = next;
    fit = false;
    redraw();
    return 1;
  }
  if (event == FL_KEYDOWN && mpv) {
    if (Fl::event_key() == ' ') {
      command({"cycle", "pause"});
      return 1;
    }
    if (Fl::event_key() == FL_Left || Fl::event_key() == FL_Right) {
      command({"seek", Fl::event_key() == FL_Left ? "-5" : "5", "relative"});
      return 1;
    }
  }
  if (event == FL_DND_ENTER || event == FL_DND_DRAG || event == FL_DND_RELEASE)
    return 1;
  return Fl_Gl_Window::handle(event);
}
bool Canvas::snapshot(const std::filesystem::path &path) {
  make_current();
  draw();
  std::vector<unsigned char> data(size_t(pixel_w()) * pixel_h() * 3);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, pixel_w(), pixel_h(), GL_RGB, GL_UNSIGNED_BYTE,
               data.data());
  std::ofstream f(path, std::ios::binary);
  f << "P6\n" << pixel_w() << " " << pixel_h() << "\n255\n";
  for (int y = pixel_h() - 1; y >= 0; --y)
    f.write((char *)data.data() + size_t(y) * pixel_w() * 3, pixel_w() * 3);
  return bool(f);
}
