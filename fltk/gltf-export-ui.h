// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "gltf-export.h"
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/fl_ask.H>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <glib.h>
class GltfExportJob {
  std::future<qimgvr::GltfExportResult> job;
  std::unique_ptr<Fl_Native_File_Chooser> nativeChooser;
public:
  bool busy() const { return job.valid(); }
  void poll(const std::function<void(const std::string &)> &message) {
    if(!job.valid() || job.wait_for(std::chrono::seconds(0))!=std::future_status::ready) return;
    try { auto result=job.get();message(result.summary()); }
    catch(const std::exception &e) { message(std::string("glTF export: ")+e.what()); }
  }
  void choose(const cv::Mat &sbs, const std::string &source,
              const std::function<void(const std::string &)> &message) {
    if(busy()) {message("A glTF export is already running");return;}
    if(sbs.empty()) {message("Prepare a stereo still image before exporting");return;}
    // Snapshot before the native modal chooser pumps navigation/slideshow events.
    auto pixels=sbs.clone();
    if(!nativeChooser) nativeChooser=std::make_unique<Fl_Native_File_Chooser>();
    auto &chooser=*nativeChooser;
    chooser.title("Export stereo image as glTF");
    chooser.type(Fl_Native_File_Chooser::BROWSE_SAVE_FILE);
    chooser.filter("glTF binary\t*.glb\nglTF JSON\t*.gltf");
    chooser.options(Fl_Native_File_Chooser::SAVEAS_CONFIRM|Fl_Native_File_Chooser::USE_FILTER_EXT);
    auto suggested=std::filesystem::path(source);suggested.replace_extension(".glb");
    if(suggested.empty() || suggested.filename()==".glb") suggested="stereo-image.glb";
    auto absolute=std::filesystem::absolute(suggested).lexically_normal().string();
    auto temporary=std::filesystem::temp_directory_path().string()+"/";
    if(!std::filesystem::path(source).has_parent_path() || absolute.rfind(temporary,0)==0 || absolute.rfind(std::string(g_get_user_runtime_dir())+"/",0)==0) {
      const char *pictures=g_get_user_special_dir(G_USER_DIRECTORY_PICTURES);
      suggested=std::filesystem::path(pictures && std::filesystem::is_directory(pictures) ? pictures : g_get_home_dir())/suggested.filename();
    }
    chooser.preset_file(suggested.string().c_str());
    int selection=chooser.show();
    if(selection) {if(selection<0) message(chooser.errmsg());return;}
    auto destination=std::filesystem::path(chooser.filename());
    if(destination.extension().empty()) {
      destination+=chooser.filter_value()==1 ? ".gltf" : ".glb";
      if(std::filesystem::exists(destination) && fl_choice("Replace the existing glTF file?","Cancel","Replace",nullptr)!=1) return;
    }
    job=std::async(std::launch::async,[pixels=std::move(pixels),destination,source] {
      return qimgvr::exportStereoGltf(pixels,destination,source);
    });
    message("Exporting stereo image as glTF...");
  }
};
