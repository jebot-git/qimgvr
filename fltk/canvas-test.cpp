// SPDX-License-Identifier: GPL-3.0-or-later
// Map the GL child after its parent has committed, exercising startup ordering.
#include "canvas.h"
#include "gpu-window.h"
#include <memory>
struct Fixture {
  dx::GpuWindow window{640,480,"qimgvr late canvas fixture"};
  std::unique_ptr<Canvas> canvas;
  Fixture() { window.end();window.show();Fl::add_timeout(.5,load,this); }
  static void load(void *data) {
    auto self=static_cast<Fixture *>(data);self->window.begin();
    self->canvas=std::make_unique<Canvas>(0,0,640,480);self->window.end();
    self->canvas->setImage(cv::Mat(240,320,CV_8UC4,cv::Scalar(235,25,45,255)));
    self->canvas->show();
  }
};
int main() {dx::init();Fixture fixture;return Fl::run();}
