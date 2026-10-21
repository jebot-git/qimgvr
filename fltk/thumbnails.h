// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "appearance.h"
#include "image.h"
#include <FL/Fl_Widget.H>
#include <future>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
class ThumbnailGrid : public Fl_Widget {
  std::vector<std::filesystem::path> files;
  int selected = 0, first = 0;
  std::map<std::string, cv::Mat> cache;
  std::future<std::pair<std::string, cv::Mat>> pending;

public:
  std::function<void(int)> chosen;
  ThumbnailGrid(int x, int y, int w, int h) : Fl_Widget(x, y, w, h) {}
  void setFiles(const std::vector<std::filesystem::path> &value, int current) {
    files = value;
    selected = current;
    first = std::max(0, current - h() / 105 + 1);
    cache.clear();
    redraw();
  }
  void setSelected(int current) {
    selected = current;
    if (selected < first)
      first = selected;
    if (selected >= first + h() / 105)
      first = std::max(0, selected - h() / 105 + 1);
    redraw();
  }
  void poll() {
    if (pending.valid() && pending.wait_for(std::chrono::seconds(0)) ==
                               std::future_status::ready) {
      auto result = pending.get();
      if (cache.size() >= 256)
        cache.erase(cache.begin());
      cache[result.first] = result.second;
      redraw();
    }
    if (pending.valid() || !visible())
      return;
    for (int i = first; i < std::min(int(files.size()), first + h() / 105 + 2);
         ++i) {
      auto path = files[i];
      if (cache.count(path.string()))
        continue;
      pending = std::async(std::launch::async, [path] {
        cv::Mat thumb;
        try {
          auto input = cv::imread(path.string(), cv::IMREAD_REDUCED_COLOR_4);
          if (!input.empty()) {
            double scale = std::min(160. / input.cols, 75. / input.rows);
            cv::resize(input, input, {}, scale, scale, cv::INTER_AREA);
            cv::cvtColor(input, thumb, cv::COLOR_BGR2RGB);
          }
        } catch (...) {
        }
        return std::make_pair(path.string(), thumb);
      });
      break;
    }
  }
  void draw() override {
    fl_push_clip(x(), y(), w(), h());
    fl_color(FL_BACKGROUND2_COLOR);
    fl_rectf(x(), y(), w(), h());
    for (int i = first; i < std::min(int(files.size()), first + h() / 105 + 2);
         ++i) {
      int top = y() + (i - first) * 105;
      if (i == selected) {
        fl_color(FL_SELECTION_COLOR);
        fl_rectf(x(), top, w(), 105);
      }
      auto it = cache.find(files[i].string());
      if (it != cache.end() && !it->second.empty()) {
        auto &image = it->second;
        fl_draw_image(image.data, x() + (w() - image.cols) / 2, top + 3,
                      image.cols, image.rows, 3, image.step);
      }
      fl_color(FL_FOREGROUND_COLOR);
      fl_font(dx::font, dx::size);
      auto name = files[i].filename().string();
      fl_draw(name.c_str(), x() + 5, top + 78, w() - 10, 26,
              FL_ALIGN_CENTER | FL_ALIGN_CLIP);
    }
    fl_pop_clip();
  }
  int handle(int e) override {
    if (e == FL_MOUSEWHEEL) {
      first = std::clamp(first + Fl::event_dy() * 2, 0,
                         std::max(0, int(files.size()) - 1));
      redraw();
      return 1;
    }
    if (e == FL_PUSH) {
      int row = first + (Fl::event_y() - y()) / 105;
      if (row >= 0 && row < int(files.size()) && chosen)
        chosen(row);
      return 1;
    }
    return Fl_Widget::handle(e);
  }
};
