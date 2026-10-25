// SPDX-License-Identifier: GPL-3.0-or-later
#include "appearance.h"
#include "canvas.h"
#include "desktop-menu.h"
#include "gpu-window.h"
#include "image.h"
#include "model-view.h"
#include "stereo.h"
#include "thumbnails.h"
#include <FL/Fl_Box.H>
#include <FL/Fl_Copy_Surface.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/Fl_Multiline_Output.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/Fl_Value_Slider.H>
#include <FL/fl_ask.H>
#include <chrono>
#include <fstream>
#include <future>
#include <glib.h>
#include <optional>
namespace fs = std::filesystem;
class Viewer : public dx::GpuWindow {
  DesktopMenuBar menu{0, 0, 1000, 30};
  ThumbnailGrid thumbnails{0, 30, 190, 610};
  Canvas canvas{0, 30, 1000, 610};
  std::shared_ptr<qimgvr::ModelCamera> modelCamera =
      std::make_shared<qimgvr::ModelCamera>();
  qimgvr::ModelView modelView{0, 30, 1000, 610, modelCamera};
  fsv::gltf::Loader modelLoader;
  std::unique_ptr<qimgvr::ModelPresenter> modelPresenter;
  std::unique_ptr<qimgvr_model::DomeMonitor> modelMonitor;
  bool modelFile = false;
  uint64_t modelRevision = 0;
  std::string modelStereoMode = "auto", modelNotice;
  std::chrono::steady_clock::time_point modelTick =
      std::chrono::steady_clock::now();
  Fl_Value_Slider seek{10, 645, 980, 20};
  Fl_Box status{10, 670, 980, 25};
  std::unique_ptr<DesktopMenu> desktop;
  std::vector<fs::path> files;
  size_t index = 0;
  fs::path current;
  std::string description;
  Json metadata;
  cv::Mat original;
  std::future<ImageData> loading;
  uint64_t generation = 0, inflight = 0;
  std::optional<fs::path> pending;
  StereoController stereo;
  GltfExportJob gltfExport;
  std::unique_ptr<Fl_Native_File_Chooser> openChooser, imageSaveChooser;
  bool edited = false, showThumbs = false;
  std::vector<cv::Mat> undo;
  std::chrono::steady_clock::time_point slide{};
  double slideshow = 0;
  static void tick(void *p) {
    ((Viewer *)p)->poll();
    Fl::repeat_timeout(.025, tick, p);
  }
  static void action(Fl_Widget *w, void *data) {
    auto self = (Viewer *)data;
    auto menu = dynamic_cast<Fl_Menu_ *>(w);
    if (menu && menu->mvalue())
      self->command(menu->mvalue()->label());
  }
  void message(std::string text) {
    if (getenv("QIMGVR_SBS_TRACE"))
      fprintf(stderr, "qimgvr: %s\n", text.c_str());
    status.copy_label(text.c_str());
  }
  void startRead(const fs::path &path) {
    inflight = generation;
    loading =
        std::async(std::launch::async, [path] { return readImage(path); });
    message("Loading " + path.filename().string() + "...");
  }
  void poll() {
    bool image = !canvas.pixels.empty();
    dx::enable_menu(menu, "Edit", image);
    dx::enable_menu(menu, "File/Save as...", image);
    dx::enable_menu(menu, "File/Metadata", image || bool(modelView.asset()));
    dx::enable_menu(menu, "File/Play in mpvr", !modelFile && !current.empty());
    dx::enable_menu(menu, "Model", modelFile && bool(modelView.asset()));
    for(auto name:{"Model/Reset model camera","Model/Play or pause animation","Model/Next animation","Model/Orbit automatically","Model/Reference image view","Model/Fit model bounds"})
      dx::enable_menu(menu,name,modelFile && bool(modelView.asset()));
    dx::enable_menu(menu, "Model/Reference image view", modelView.asset() && modelView.asset()->stereoReference.has_value());
    dx::enable_menu(menu, "View/Actual pixels", !modelFile);
    for(auto name:{"Stereo/Input: Auto","Stereo/Input: 2D","Stereo/Input: Full SBS","Stereo/Input: Half SBS"})
      dx::enable_menu(menu,name,!modelFile);
    dx::enable_menu(menu, "Edit/Undo", !undo.empty());
    dx::enable_menu(menu, "Playback", canvas.video());
    dx::enable_menu(menu, "View/Next", files.size() > 1);
    dx::enable_menu(menu, "View/Previous", files.size() > 1);

    pollModel();
    canvas.poll();
    thumbnails.poll();
    stereo.pump();
    gltfExport.poll([this](const std::string &text){message(text);});
    dx::enable_menu(menu,"File/Export as glTF...",exportReady() && !gltfExport.busy());
    if (loading.valid() && loading.wait_for(std::chrono::seconds(0)) ==
                               std::future_status::ready) {
      try {
        auto result = loading.get();
        if (inflight == generation && !pending && !modelFile) {
          original = result.rgba;
          canvas.setImage(original);
          metadata = result.metadata;
          description = result.description;
          edited = false;
          undo.clear();
          sourceChanged();
          message(current.filename().string() + " · " +
                  std::to_string(original.cols) + " × " +
                  std::to_string(original.rows));
        }
      } catch (const std::exception &e) {
        if (inflight == generation)
          message(e.what());
      }
      if (pending) {
        auto p = *pending;
        pending.reset();
        startRead(p);
      }
    }
    if(modelFile && modelView.asset() && !modelView.asset()->animations.empty()) {
      auto &clips=modelView.asset()->animations;
      seek.activate();seek.range(0,clips[modelCamera->animation].duration);
      if(Fl::pushed()!=&seek)seek.value(modelCamera->seconds);
    } else if (canvas.video()) {
      double duration = canvas.duration();
      if (duration > 0) {
        seek.activate();
        seek.range(0, duration);
        if (Fl::pushed() != &seek)
          seek.value(canvas.position());
      }
    } else
      seek.deactivate();
    if (slideshow > 0 && std::chrono::steady_clock::now() - slide >
                             std::chrono::duration<double>(slideshow)) {
      slide = std::chrono::steady_clock::now();
      navigate(1);
    }
  }
  void pollModel() {
    if (!modelFile)
      return;
    auto now = std::chrono::steady_clock::now();
    double dt = std::clamp(
        std::chrono::duration<double>(now - modelTick).count(), 0., .1);
    modelTick = now;
    auto loaded = modelLoader.snapshot();
    if (loaded.revision != modelRevision) {
      modelRevision = loaded.revision;
      if (loaded.model != modelView.asset()) {
        modelView.setModel(loaded.model);
        if (modelPresenter)
          modelPresenter->setModel(loaded.model);
        if (loaded.model &&
            modelCamera->animation >= int(loaded.model->animations.size()))
          modelCamera->animation = 0;
      }
      if (!loaded.error.empty()) {
        message(loaded.error);
        description = loaded.error;
      } else if (loaded.model) {
        auto m = loaded.model;
        description = Json{
            {"format", "glTF 2.0"},
            {"source", current.string()},
            {"triangles", m->triangleCount()},
            {"meshes", m->meshes.size()},
            {"materials", m->materials.size()},
            {"images", m->images.size()},
            {"animations", m->animations.size()},
            {"warnings",
             m->warnings}}.dump(2);
        message(current.filename().string() + " · " +
                std::to_string(m->triangleCount()) +
                " triangles · Drag: orbit · Shift/middle: pan · Wheel: zoom · "
                "R: reset");
      }
    }
    auto m = modelView.asset();
    if (m && modelCamera->playing && !m->animations.empty()) {
      auto duration = m->animations[modelCamera->animation].duration;
      modelCamera->seconds =
          duration > 0 ? std::fmod(modelCamera->seconds + dt, duration) : 0;
      modelView.redraw();
    }
    if (m && modelCamera->orbit) {
      modelCamera->yaw += dt * .18f;
      modelView.redraw();
    }
    auto mode =
        modelMonitor ? modelMonitor->snapshot() : qimgvr_model::DomeMode{};
    bool present = m && (modelStereoMode == "always" ||
                         (modelStereoMode == "auto" && mode.known && mode.xr &&
                          mode.stereoSupported));
    if (present) {
      if (!modelPresenter)
        modelPresenter = std::make_unique<qimgvr::ModelPresenter>(
            modelCamera, [this](const std::string &n) { command(n); });
      modelPresenter->setModel(m);
      modelPresenter->presentation = mode;
      if (!modelPresenter->shown())
        modelPresenter->show();
      modelPresenter->redraw();
      modelView.redraw();
    } else if (modelPresenter)
      modelPresenter->hide();
    auto error = !modelView.failure().empty() ? modelView.failure()
                 : modelPresenter             ? modelPresenter->failure()
                                              : std::string();
    if (!error.empty() && error != modelNotice) {
      modelNotice = error;
      message(error);
    } else if (error.empty() && !modelNotice.empty()) {
      modelNotice.clear();
      message("glTF rendering resumed");
    }
  }
  void sourceChanged() {
    stereo.setSource(canvas.pixels, current.string(), metadata);
    copy_label((current.filename().string() + (edited ? " *" : "") +
                " — qimgvr (FLTK)")
                   .c_str());
  }
  void remember() {
    if (canvas.pixels.empty())
      return;
    uint64_t bytes = 0;
    for (auto &image : undo)
      bytes += image.total() * image.elemSize();
    if (undo.size() >= 8 ||
        bytes + canvas.pixels.total() * 4 > 256 * 1024 * 1024)
      undo.clear();
    undo.push_back(canvas.pixels.clone());
  }
  bool exportReady() const {
    return !modelFile && !loading.valid() && !pending.has_value() && !canvas.video() && stereo.canExport();
  }
  void save() {
    if (canvas.pixels.empty())
      return;
    if(!imageSaveChooser) imageSaveChooser=std::make_unique<Fl_Native_File_Chooser>();
    auto &chooser=*imageSaveChooser;
    chooser.title("Save edited image as");
    chooser.type(Fl_Native_File_Chooser::BROWSE_SAVE_FILE);
    chooser.options(Fl_Native_File_Chooser::SAVEAS_CONFIRM);
    chooser.preset_file(current.string().c_str());
    if (chooser.show())
      return;
    auto destination = fs::path(chooser.filename());
    try {
      if (destination == current && fs::exists(destination) &&
          fl_choice("Replace the original image?", "Cancel", "Replace",
                    nullptr) != 1)
        return;
      saveImage(destination, canvas.pixels, current);
      message("Saved " + destination.string());
      edited = false;
      copy_label((current.filename().string() + " — qimgvr (FLTK)").c_str());
    } catch (const std::exception &e) {
      message(e.what());
    }
  }
  void settings(bool write) {
    auto home = getenv("XDG_CONFIG_HOME");
    auto root =
        fs::path(home ? home
                      : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") +
                            "/.config") /
        "qimgvr/fltk.json";
    try {
      if (write) {
        fs::create_directories(root.parent_path());
        auto temp = root;
        temp += ".tmp";
        std::ofstream(temp) << Json{
            {"thumbnails", showThumbs},
            {"slideshow_seconds",
             slideshow}}.dump(2);
        fs::rename(temp, root);
      } else {
        std::ifstream f(root);
        Json value;
        f >> value;
        showThumbs = value.value("thumbnails", false);
      }
    } catch (...) {
    }
  }

public:
  Viewer() : dx::GpuWindow(1000, 700, "qimgvr — FLTK OpenGL") {
    xclass("qimgvr");
    modelView.hide();
    for (auto item : std::vector<std::pair<const char *, int>>{
             {"File/Open...", FL_CTRL + 'o'},
             {"File/Save as...", FL_CTRL + FL_SHIFT + 's'},
             {"File/Export as glTF...", 0},
             {"File/Metadata", 'i'},
             {"File/Play in mpvr", 0},
             {"File/Quit", FL_CTRL + 'q'},
             {"Edit/Rotate clockwise", 'e'},
             {"Edit/Rotate counterclockwise", 'q'},
             {"Edit/Flip horizontally", 'h'},
             {"Edit/Flip vertically", 'v'},
             {"Edit/Crop selection", 'c'},
             {"Edit/Undo", FL_CTRL + 'z'},
             {"Edit/Reset edits", 0},
             {"Edit/Copy image", FL_CTRL + 'c'},
             {"View/Previous", FL_Left},
             {"View/Next", FL_Right},
             {"View/Fit", 'f'},
             {"View/Actual pixels", '1'},
             {"View/Thumbnails", 't'},
             {"View/Slideshow...", 0},
             {"View/Fullscreen", FL_F + 11},
             {"Stereo/Off", 0},
             {"Stereo/Auto when headset connected", 0},
             {"Stereo/Always (XR or preview)", 's'},
             {"Stereo/Input: Auto", 0},
             {"Stereo/Input: 2D", 0},
             {"Stereo/Input: Full SBS", 0},
             {"Stereo/Input: Half SBS", 0},
             {"Model/Reset model camera", 'r'},
             {"Model/Play or pause animation", ' '},
             {"Model/Next animation", 0},
             {"Model/Reference image view", 0},
             {"Model/Fit model bounds", 0},
             {"Model/Orbit automatically", 0},
             {"Playback/Play or pause", ' '},
             {"Playback/Seek backward", FL_ALT + FL_Left},
             {"Playback/Seek forward", FL_ALT + FL_Right},
             {"Playback/Mute", 'm'},
             {"Playback/Volume up", '+'},
             {"Playback/Volume down", '-'}})
      menu.add(item.first, item.second, action, this);
    end();
    resizable(canvas);
    size_range(400, 300);
    status.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    seek.type(FL_HOR_NICE_SLIDER);
    seek.callback(
        [](Fl_Widget *, void *p) {
          auto self = (Viewer *)p;
          if(self->modelFile){self->modelCamera->seconds=self->seek.value();self->modelView.redraw();return;}
          self->canvas.command(
              {"seek", std::to_string(self->seek.value()), "absolute"});
        },
        this);
    seek.deactivate();
    canvas.failed = [this](std::string error) { message(error); };
    canvas.edited = [this] {
      edited = true;
      sourceChanged();
    };
    canvas.cropped = [this](cv::Rect region) {
      remember();
      canvas.setImage(canvas.pixels(region).clone());
      edited = true;
      sourceChanged();
    };
    thumbnails.chosen = [this](int row) {
      if (row >= 0 && row < int(files.size()))
        load(files[row]);
    };
    stereo.commandRequested = [this](const std::string &value) { command(value); };
    stereo.message = [this](const std::string &value) { if(!modelFile)message(value); };
    settings(false);
    showThumbnails(showThumbs);
    menu.layoutChanged = [this] { resize(x(), y(), w(), h()); };
    desktop = std::make_unique<DesktopMenu>(*this, menu);
    Fl::add_timeout(.025, tick, this);
  }
  void hide() override {
    if(modelPresenter)modelPresenter->hide();
    dx::GpuWindow::hide();
  }
  ~Viewer() {
    Fl::remove_timeout(tick, this);
    if (loading.valid())
      loading.wait();
    settings(true);
  }
  void showThumbnails(bool value) {
    showThumbs = value;
    if (value) thumbnails.show();
    else thumbnails.hide();
    resize(x(), y(), w(), h());
    redraw();
  }
  void resize(int x, int y, int w, int h) override {
    dx::GpuWindow::resize(x, y, w, h);
    const int top = menu.universal() ? 0 : 30;
    menu.resize(0, 0, w, top);
    thumbnails.resize(0, top, 190, std::max(1, h - top - 60));
    canvas.resize(showThumbs ? 190 : 0, top,
                  std::max(1, w - (showThumbs ? 190 : 0)), std::max(1, h - top - 60));
    seek.resize(10, h - 55, w - 20, 20);
    modelView.resize(canvas.x(),canvas.y(),canvas.w(),canvas.h());
    status.resize(10, h - 30, w - 20, 25);
  }
  void open() {
    // Keep the native window alive after modal dismissal: Wayland may still
    // have key-repeat or pointer events referring to its widgets.
    if(!openChooser) openChooser=std::make_unique<Fl_Native_File_Chooser>();
    auto &chooser=*openChooser;
    chooser.title("Open image, video or glTF");
    chooser.type(Fl_Native_File_Chooser::BROWSE_FILE);
    if (chooser.show() == 0)
      load(chooser.filename());
  }
  void load(const fs::path &path) {
    try {
      auto target = fs::canonical(path);
      if (edited && fl_choice("Discard unsaved image edits?", "Cancel",
                              "Discard", nullptr) != 1)
        return;
      current = target;
      ++generation;
      modelFile = fsv::gltf::supported(current);
      modelNotice.clear();
      if (modelFile) {
        pending.reset();
        canvas.setImage({});
        canvas.hide();
        modelView.show();
        resizable(modelView);
        original.release();
        metadata = Json::object();
        undo.clear();
        stereo.setSource({}, current.string(), {});
        modelCamera->reset();
        modelCamera->seconds = 0;
        modelCamera->animation = 0;
        modelView.setModel({});
        if (modelPresenter)
          modelPresenter->setModel({});
        if (!modelMonitor)
          modelMonitor = std::make_unique<qimgvr_model::DomeMonitor>();
        modelLoader.open(current);
        modelRevision = 0;
        modelTick = std::chrono::steady_clock::now();
        copy_label((current.filename().string() + " — qimgvr glTF").c_str());
        message("Loading glTF " + current.filename().string() + "...");
      } else {
        modelLoader.clear();
        modelView.setModel({});
        modelView.hide();
        canvas.show();
        resizable(canvas);
        if (modelPresenter) {
          modelPresenter->hide();
          modelPresenter->setModel({});
        }
        modelMonitor.reset();
      }
      if (modelFile) {
        // Model loading is asynchronous and never enters the image/SBS worker.
      } else if (videoExtension(current)) {
        pending.reset();
        canvas.setVideo(current.string());
        original.release();
        metadata = Json::object();
        stereo.setSource({}, current.string(), {});
        copy_label((current.filename().string() + " — qimgvr (FLTK)").c_str());
        message("Opening video...");
      } else if (loading.valid())
        pending = current;
      else
        startRead(current);
      files.clear();
      std::error_code ec;
      for (auto &file : fs::directory_iterator(current.parent_path(), ec))
        if (file.is_regular_file(ec) &&
            (imageExtension(file.path()) || videoExtension(file.path()) ||
             fsv::gltf::supported(file.path())))
          files.push_back(file.path());
      std::sort(files.begin(), files.end());
      auto it = std::find(files.begin(), files.end(), current);
      index = it == files.end() ? 0 : it - files.begin();
      thumbnails.setFiles(files, index);
      edited = false;
      slide = std::chrono::steady_clock::now();
    } catch (const std::exception &e) {
      message(e.what());
    }
  }
  void navigate(int delta) {
    if (files.empty())
      return;
    size_t next = (index + files.size() + delta) % files.size();
    load(files[next]);
  }
  void command(const std::string &name) {
    if (name == "Open...")
      open();
    else if (name == "Next")
      navigate(1);
    else if (name == "Previous")
      navigate(-1);
    else if (name == "Fit") {
      if(modelFile)modelView.reset();else canvas.resetView();
    }
    else if (name == "Actual pixels") {
      canvas.fit = false;
      canvas.zoom = 1;
      canvas.panX = canvas.panY = 0;
      canvas.redraw();
    } else if (name == "Thumbnails")
      showThumbnails(!showThumbs);
    else if (name == "Fullscreen") {
      if (fullscreen_active())
        fullscreen_off();
      else
        fullscreen();
    } else if (name == "Play in mpvr") {
      if (!current.empty())
        dx::spawn({"mpvr", current.string()});
    } else if (name == "Save as...")
      save();
    else if (name == "Export as glTF...") {
      try {
        if(!exportReady()) throw std::runtime_error("Wait for the current stereo still image to finish rendering");
        gltfExport.choose(stereo.exportPixels(),current.string(),[this](const std::string &text){message(text);});
      }
      catch(const std::exception &e) {message(e.what());}
    } else if (name == "Metadata") {
      auto dialog = new dx::GpuWindow(650, 500, modelFile?"glTF model metadata":"Image metadata");
      auto text = new Fl_Multiline_Output(10, 10, 630, 480);
      text->value(description.c_str());
      dialog->end();
      dialog->resizable(text);
      dx::style(dialog);
      dialog->callback([](Fl_Widget *w, void *) { Fl::delete_widget(w); });
      dialog->show();
    } else if (name == "Rotate clockwise" ||
               name == "Rotate counterclockwise") {
      remember();
      canvas.rotate(name == "Rotate clockwise");
    } else if (name == "Flip horizontally" || name == "Flip vertically") {
      remember();
      canvas.flip(name == "Flip horizontally");
    } else if (name == "Crop selection") {
      canvas.cropMode = true;
      message("Drag a rectangle over the image to crop.");
    } else if (name == "Undo") {
      if (!undo.empty()) {
        auto pixels = undo.back();
        undo.pop_back();
        canvas.setImage(pixels);
        edited = true;
        sourceChanged();
      }
    } else if (name == "Reset edits") {
      if (!original.empty()) {
        remember();
        canvas.setImage(original.clone());
        edited = false;
        sourceChanged();
      }
    } else if (name == "Copy image") {
      if (!canvas.pixels.empty()) {
        cv::Mat rgb;
        cv::cvtColor(canvas.pixels, rgb, cv::COLOR_RGBA2RGB);
        {
          Fl_Copy_Surface clip(rgb.cols, rgb.rows);
          Fl_Surface_Device::push_current(&clip);
          fl_draw_image(rgb.data, 0, 0, rgb.cols, rgb.rows, 3, rgb.step);
          Fl_Surface_Device::pop_current();
        }
        message("Image copied");
      }
    } else if (name == "Slideshow...") {
      auto value = fl_input("Seconds between images (0 stops):",
                            std::to_string(slideshow).c_str());
      if (value)
        try {
          slideshow = std::clamp(std::stod(value), 0., 3600.);
          slide = std::chrono::steady_clock::now();
        } catch (...) {
          message("Enter a number of seconds");
        }
    } else if(name=="Reset model camera") {
      modelView.reset();
    } else if(name=="Reference image view") {
      modelView.reset();
    } else if(name=="Fit model bounds") {
      modelView.reset();modelCamera->reference=false;
    } else if(name=="Play or pause animation") {
      modelCamera->playing=!modelCamera->playing;
    } else if(name=="Next animation") {
      if(auto m=modelView.asset();m && !m->animations.empty()) {
        modelCamera->animation=(modelCamera->animation+1)%m->animations.size();modelCamera->seconds=0;
        message("Animation: "+m->animations[modelCamera->animation].name);
      }
    } else if(name=="Orbit automatically") {
      modelCamera->orbit=!modelCamera->orbit;
    } else if(name=="Close stereo view") {
      setStereo("off");
    } else if (name == "Off")
      setStereo("off");
    else if (name == "Auto when headset connected")
      setStereo("auto");
    else if (name == "Always (XR or preview)")
      setStereo(modelFile?(modelStereoMode=="always"?"off":"always"):(stereo.active()?"off":"always"));
    else if (name == "Input: Auto")
      stereo.setLayout("auto");
    else if (name == "Input: 2D")
      stereo.setLayout("2d");
    else if (name == "Input: Full SBS")
      stereo.setLayout("full-sbs");
    else if (name == "Input: Half SBS")
      stereo.setLayout("half-sbs");
    else if (name == "Play or pause")
      canvas.command({"cycle", "pause"});
    else if (name == "Seek backward")
      canvas.command({"seek", "-5", "relative"});
    else if (name == "Seek forward")
      canvas.command({"seek", "5", "relative"});
    else if (name == "Mute")
      canvas.command({"cycle", "mute"});
    else if (name == "Volume up")
      canvas.command({"add", "volume", "5"});
    else if (name == "Volume down")
      canvas.command({"add", "volume", "-5"});
    else if (name == "Quit")
      hide();
  }
  int handle(int e) override {
    if (e == FL_PASTE) {
      std::string value(Fl::event_text(), Fl::event_length());
      if (value.rfind("file://", 0) == 0) {
        auto end = value.find_first_of("\r\n");
        value = value.substr(0, end);
        GError *error = nullptr;
        auto path = g_filename_from_uri(value.c_str(), nullptr, &error);
        if (path) {
          load(path);
          g_free(path);
        }
        if (error)
          g_error_free(error);
        return 1;
      }
    }
    return dx::GpuWindow::handle(e);
  }
  void setStereo(std::string mode) { modelStereoMode=mode;stereo.setMode(std::move(mode)); }
  void setInputLayout(std::string value) { stereo.setLayout(std::move(value)); }
  bool smoke(const fs::path &output) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while ((loading.valid() || canvas.video() ||
            (modelFile && modelLoader.snapshot().loading)) &&
           std::chrono::steady_clock::now() < deadline) {
      Fl::wait(.025);
      if (canvas.video() && canvas.duration() > 0)
        break;
    }
    // Fast model loads can finish before FLTK's first timer fires. Settle by
    // elapsed time and explicitly consume loader state before taking QA frames.
    auto settle=std::chrono::steady_clock::now()+std::chrono::milliseconds(250);
    do { poll();Fl::wait(.025); } while(std::chrono::steady_clock::now()<settle);
    if (modelFile) {
      modelView.snapshot(output.string() + ".canvas.ppm");
      auto m = modelView.asset();
      std::ofstream(output.string() + ".json") << Json{
          {"model", true},
          {"loaded", bool(m)},
          {"error", !modelView.failure().empty()
                        ? modelView.failure()
                        : modelLoader.snapshot().error},
          {"triangles", m ? m->triangleCount() : 0},
          {"images", m ? m->images.size() : 0},
          {"animations", m ? m->animations.size() : 0},
          {"frames", modelView.frames},
          {"stereoFrames", modelPresenter ? modelPresenter->stereoFrames : 0},
          {"referenceView",
           m && m->stereoReference.has_value() &&
               modelCamera->reference}}.dump(2);
    } else {
      canvas.flush();
      canvas.snapshot(output.string() + ".canvas.ppm");
    }
    dx::screenshot(*this, output.c_str());
    return !modelFile || (modelView.asset() && modelView.failure().empty() &&
                          modelLoader.snapshot().error.empty());
  }
};
int main(int argc, char **argv) try {
  // SBS is an opt-in identity for the image presenter only. FLTK otherwise
  // inherits the first window's class into its file and confirmation dialogs.
  Fl_Window::default_xclass("qimgvr");
  std::string presenter, title = "qimgvr — SBS image", sbs = "auto",
                         inputLayout = "auto";
  fs::path file, smoke, exportGltf;
  bool dome = false;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if(arg=="--help") {
      puts("qimgvr [--sbs=auto|always|off] [--input-layout=auto|2d|full-sbs|half-sbs] IMAGE|VIDEO|MODEL.glb|MODEL.gltf\n"
           "qimgvr --export-gltf OUTPUT.glb|OUTPUT.gltf --input-layout=full-sbs|half-sbs STEREO_IMAGE\n"
           "glTF export is a prototype textured relief with approximate stereo depth.");
      return 0;
    }
    if(arg=="--export-gltf") {
      if(++i>=argc) throw std::invalid_argument("--export-gltf requires a filename");
      exportGltf=argv[i];continue;
    }
    if (arg == "--version") {
      puts("qimgvr FLTK 1.4.5 OpenGL + OpenCV + Exiv2 + libmpv");
      return 0;
    }
    if (arg == "--build-options") {
      puts("FLTK=1.4.5 OPENGL=ON EXIV2=ON OPENCV=ON MPV=ON SBS=ON");
      return 0;
    }
    if (arg == "--no-sbs") {
      sbs = "off";
      continue;
    }
    if (arg == "--sbs") {
      sbs = "always";
      if (i + 1 < argc) {
        std::string next = argv[i + 1];
        if (next == "auto" || next == "always" || next == "off") {
          sbs = next;
          ++i;
        }
      }
      continue;
    }
    if (arg.rfind("--sbs=", 0) == 0) {
      sbs = arg.substr(6);
      continue;
    }
    if (arg.rfind("--input-layout=", 0) == 0) {
      inputLayout = arg.substr(15);
      continue;
    }
    if ((arg == "--sbs-present" || arg == "--sbs-present-encoded" ||
         arg == "--sbs-title" || arg == "--smoke-test" || arg == "--sbs" ||
         arg == "--input-layout") &&
        i + 1 < argc) {
      auto value = std::string(argv[++i]);
      if (arg == "--sbs-present")
        presenter = value;
      else if (arg == "--sbs-present-encoded") {
        gsize length;
        auto text = g_base64_decode(value.c_str(), &length);
        presenter.assign((char *)text, length);
        g_free(text);
      } else if (arg == "--sbs-title")
        title = value;
      else if (arg == "--smoke-test")
        smoke = value;
      else if (arg == "--sbs")
        sbs = value;
      else
        inputLayout = value;
    } else if (arg == "--dome-present")
      dome = true;
    else if (argv[i][0] != '-')
      file = argv[i];
  }
  if (sbs != "auto" && sbs != "always" && sbs != "off")
    throw std::invalid_argument("--sbs requires auto, always or off");
  if (inputLayout != "auto" && inputLayout != "2d" &&
      inputLayout != "full-sbs" && inputLayout != "half-sbs")
    throw std::invalid_argument("Unsupported SBS input layout");
  if(!exportGltf.empty()) {
    if(file.empty() || (inputLayout!="full-sbs" && inputLayout!="half-sbs"))
      throw std::invalid_argument("--export-gltf requires a rendered stereo image and explicit --input-layout=full-sbs or half-sbs");
    if(fs::absolute(file).lexically_normal()==fs::absolute(exportGltf).lexically_normal())
      throw std::invalid_argument("Export must not replace the source stereo image");
    auto pixels=readImage(file).rgba;
    if(pixels.cols%2) throw std::invalid_argument("SBS image width must be even");
    if(inputLayout=="half-sbs") {
      cv::Mat left,right,expanded;
      int eye=pixels.cols/2;
      cv::resize(pixels(cv::Rect(0,0,eye,pixels.rows)),left,cv::Size(eye*2,pixels.rows),0,0,cv::INTER_CUBIC);
      cv::resize(pixels(cv::Rect(eye,0,eye,pixels.rows)),right,cv::Size(eye*2,pixels.rows),0,0,cv::INTER_CUBIC);
      cv::hconcat(left,right,expanded);pixels=expanded;
    }
    puts(qimgvr::exportStereoGltf(pixels,exportGltf,file.string()).summary().c_str());
    return 0;
  }
  if (!presenter.empty()) {
    if (dome || getenv("WAYLAND_DISPLAY"))
      setenv("FLTK_BACKEND", "wayland", 1);
    dx::init();
    StereoWindow viewer;
    viewer.copy_label(title.c_str());
    if (dome) {
      viewer.xclass("org.qimgvr.Sbs");
      viewer.border(0);
    }
    viewer.watch(presenter);
    viewer.show();
    if (!dome)
      viewer.fullscreen();
    if (!smoke.empty()) {
      Fl::check();
      return viewer.snapshot(smoke) ? 0 : 1;
    }
    return Fl::run();
  }
  dx::init();
  Fl::lock();
  Viewer viewer;
  viewer.setInputLayout(inputLayout);
  viewer.setStereo(sbs);
  if (!file.empty())
    viewer.load(file);
  dx::style(&viewer);
  viewer.show();
  if (!smoke.empty()) {
    if(!viewer.smoke(smoke))return 1;
    puts("{\"toolkit\":\"FLTK\",\"renderer\":\"OpenGL\",\"mpv\":true,\"sbs\":"
         "true}");
    return 0;
  }
  return Fl::run();
} catch (const std::exception &e) {
  fprintf(stderr, "qimgvr: %s\n", e.what());
  return 1;
}
