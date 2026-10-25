// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "canvas.h"
#include "gltf-export-ui.h"
#include "desktop-menu.h"
#include "json-process.h"
#include "stereo-controls.h"
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Menu_Button.H>
#include <chrono>
class StereoWindow : public Canvas {
  DesktopMenuBar menu{0, 0, 0, 0};
  std::unique_ptr<DesktopMenu> desktop;
  std::string endpoint;
  GltfExportJob gltfExport;
  static void action(Fl_Widget *widget, void *data) {
    auto item = ((Fl_Menu_ *)widget)->mvalue();
    if (item)
      ((StereoWindow *)data)->command(item->label());
  }
  void command(const std::string &name) {
    if (name == "Export as glTF...") {
      try { gltfExport.choose(pixels,!exportSourceName.empty() ? exportSourceName : watched.empty() ? "stereo-image.png" : watched.string(),
                             [this](const std::string &text){copy_label(("qimgvr — "+text).c_str());}); }
      catch(const std::exception &e) {fl_alert("%s",e.what());}
    } else if (name == "Fit")
      resetView();
    else if (name == "Actual pixels") {
      fit = false;
      zoom = 1;
      panX = panY = 0;
      redraw();
    } else if (name == "Fullscreen") {
      if (fullscreen_active())
        fullscreen_off();
      else
        fullscreen();
    } else {
      if (ownerCommand)
        ownerCommand(name);
      else
        StereoControls::send(endpoint, name);
      if (name == "Close stereo view")
        hide();
    }
  }
  std::filesystem::path watched;
  std::filesystem::file_time_type modified{};
  static void tick(void *p) {
    auto s = (StereoWindow *)p;
    s->gltfExport.poll([s](const std::string &text){s->copy_label(("qimgvr — "+text).c_str());});
    dx::enable_menu(s->menu,"File/Export as glTF...",!s->pixels.empty() && !s->gltfExport.busy());
    try {
      if (!s->watched.empty()) {
        if (!std::filesystem::exists(s->watched.parent_path())) {
          s->hide();
          return;
        }
        auto stamp = std::filesystem::last_write_time(s->watched);
        if (stamp != s->modified) {
          auto image = readImage(s->watched);
          if (image.rgba.cols % 2)
            throw std::runtime_error("Full SBS image width must be even");
          s->Canvas::setImage(image.rgba);
          s->modified = stamp;
        }
      }
    } catch (const std::exception &) {
    }
    Fl::repeat_timeout(.15, tick, p);
  }

public:
  std::function<void(const std::string &)> ownerCommand;
  std::string exportSourceName;
  StereoWindow() : Canvas(0, 0, 1280, 720) {
    if (auto value = getenv("QIMGVR_SBS_CONTROL"))
      endpoint = value;
    menu.hide();
    menu.add("File/Previous", FL_Left, action, this);
    menu.add("File/Next", FL_Right, action, this);
    menu.add("File/Export as glTF...", 0, action, this);
    menu.add("File/Close stereo view", FL_Escape, action, this);
    menu.add("View/Fit", 'f', action, this);
    menu.add("View/Actual pixels", '1', action, this);
    menu.add("View/Fullscreen", FL_F + 11, action, this);
    dx::style(&menu);
    desktop = std::make_unique<DesktopMenu>(*this, menu);
    callback(
        [](Fl_Widget *, void *data) {
          ((StereoWindow *)data)->command("Close stereo view");
        },
        this);
    copy_label("qimgvr — SBS image");
    stereo = true;
    size_range(200, 100);
    Fl::add_timeout(.15, tick, this);
  }
  ~StereoWindow() { Fl::remove_timeout(tick, this); }
  void setImage(const cv::Mat &image) {
    if (image.cols % 2)
      throw std::runtime_error("Full SBS image width must be even");
    Canvas::setImage(image);
  }
  void watch(const std::filesystem::path &p) {
    watched = p;
    modified = {};
    tick(this);
    Fl::remove_timeout(tick, this);
    Fl::add_timeout(.15, tick, this);
  }
  int handle(int e) override {
    dx::enable_menu(menu, "File/Next", bool(ownerCommand) || !endpoint.empty());
    dx::enable_menu(menu, "File/Previous",
                    bool(ownerCommand) || !endpoint.empty());
    if ((e == FL_KEYDOWN || e == FL_SHORTCUT) && menu.test_shortcut())
      return 1;
    if (e == FL_PUSH && Fl::event_button() == FL_RIGHT_MOUSE) {
      Fl_Menu_Button popup(Fl::event_x(), Fl::event_y(), 0, 0);
      popup.type(Fl_Menu_Button::POPUP3);
      popup.menu(menu.menu());
      dx::style(&popup);
      popup.popup();
      return 1;
    }
    return Canvas::handle(e);
  }
};
class StereoController {
  JsonProcess worker;
  std::filesystem::path temporary;
  cv::Mat source;
  Json metadata;
  std::string sourceName, mode = "off", layout = "auto";
  uint64_t generation = 0, completed = 0, inflight = 0, outputGeneration = 0;
  bool busy = false, hidePending = false, connected = false;
  std::filesystem::path currentOutput;
  std::unique_ptr<StereoWindow> preview;
  std::unique_ptr<StereoControls> controls;
  std::chrono::steady_clock::time_point probe{};
  void start();
  void dispatch();
  void response(const Json &);

public:
  std::function<void(const std::string &)> message, commandRequested;
  StereoController();
  ~StereoController();
  void setSource(const cv::Mat &, std::string, Json);
  void setMode(std::string);
  void setLayout(std::string);
  void pump();
  bool active() const { return mode != "off"; }
  bool canExport() const { return active() && outputGeneration==generation && !currentOutput.empty(); }
  cv::Mat exportPixels() const {
    if(!canExport()) throw std::runtime_error("Wait for the current stereo image to finish rendering");
    return readImage(currentOutput).rgba;
  }
  std::string currentMode() const { return mode; }
};
