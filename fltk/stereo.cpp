// SPDX-License-Identifier: GPL-3.0-or-later
#include "stereo.h"
#include <limits.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
StereoController::StereoController() {
  char path[] = "/tmp/qimgvr-fltk-sbs-XXXXXX";
  if (auto p = mkdtemp(path))
    temporary = p;
  worker.response = [this](const Json &object) {
    if (getenv("QIMGVR_SBS_TRACE"))
      fprintf(stderr, "SBS response: %s\n", object.dump().c_str());
    response(object);
  };
  worker.diagnostic = [](const std::string &value) {
    if (getenv("QIMGVR_SBS_TRACE"))
      fwrite(value.data(), 1, value.size(), stderr);
  };
  worker.failed = [this](const std::string &text) {
    busy = false;
    completed = generation;
    if (message)
      message(text);
  };
}
StereoController::~StereoController() {
  worker.stop();
  controls.reset();
  if (!temporary.empty()) {
    std::error_code ec;
    std::filesystem::remove_all(temporary, ec);
  }
}
void StereoController::start() {
  if (worker.running())
    return;
  if (temporary.empty())
    throw std::runtime_error("Cannot create SBS working directory");
  const char *selected = getenv("QIMGV_SBS_PYTHON");
  std::string python = selected ? selected
                       : std::filesystem::exists("/opt/arc-sbs/venv/bin/python")
                           ? "/opt/arc-sbs/venv/bin/python"
                           : "python3";
  std::vector<std::string> args = {python, "-u", "-m", "arc_sbs.image_worker"};
  if (dx::dome()) {
    args.push_back("--xr-backend");
    args.push_back("domed");
  }
  if (auto device = getenv("QIMGVR_SBS_DEVICE")) {
    args.push_back("--device");
    args.push_back(device);
  }
  if (auto renderer = getenv("QIMGVR_SBS_RENDERER")) {
    args.push_back("--renderer");
    args.push_back(renderer);
  }
  if (!controls)
    controls = std::make_unique<StereoControls>(temporary);
  worker.start(args, {{"QIMGVR_SBS_CONTROL", controls->endpoint()}});
  busy = false;
  hidePending = true;
}
void StereoController::setSource(const cv::Mat &image, std::string name,
                                 Json tags) {
  source = image.clone();
  sourceName = std::move(name);
  metadata = std::move(tags);
  ++generation;
  hidePending = true;
  if (active() && !source.empty())
    try {
      start();
    } catch (const std::exception &e) {
      if (message)
        message(e.what());
    }
}
void StereoController::setMode(std::string value) {
  mode = std::move(value);
  ++generation;
  hidePending = true;
  if (active() && !source.empty())
    try {
      start();
    } catch (const std::exception &e) {
      if (message)
        message(e.what());
    }
}
void StereoController::setLayout(std::string value) {
  layout = std::move(value);
  ++generation;
  hidePending = true;
  if (active() && !source.empty())
    try {
      start();
    } catch (const std::exception &e) {
      if (message)
        message(e.what());
    }
}
void StereoController::dispatch() {
  if (busy || !worker.running())
    return;
  if (hidePending) {
    hidePending = false;
    busy = true;
    worker.send({{"action", "hide"}});
    return;
  }
  if (active() && !source.empty() && completed != generation) {
    inflight = generation;
    auto input = temporary / (std::to_string(inflight) + "-input.png"),
         output = temporary / (std::to_string(inflight) + ".png");
    saveImage(input, source);
    busy = true;
    if (message)
      message("Preparing SBS image...");
    worker.send({{"action", "convert"},
                 {"id", inflight},
                 {"mode", mode},
                 {"source_name", sourceName},
                 {"metadata", metadata},
                 {"input_layout", layout},
                 {"input", input.string()},
                 {"output", output.string()}});
    return;
  }
  if (active() &&
      std::chrono::steady_clock::now() - probe > std::chrono::seconds(3)) {
    probe = std::chrono::steady_clock::now();
    busy = true;
    worker.send({{"action", "probe"}});
  }
}
void StereoController::response(const Json &object) {
  busy = false;
  auto action = object.value("action", std::string());
  bool was = connected;
  if (object.contains("connected"))
    connected = object.value("connected", false);
  uint64_t id = object.value("id", Json()).is_number_unsigned()
                    ? object["id"].get<uint64_t>()
                : object.value("id", Json()).is_number_integer()
                    ? uint64_t(object["id"].get<int64_t>())
                    : 0;
  if (action == "convert" && (id != generation || !active())) {
    std::error_code ec;
    std::filesystem::remove(temporary / (std::to_string(id) + ".png"), ec);
    std::filesystem::remove(temporary / (std::to_string(id) + "-input.png"),
                            ec);
    return;
  }
  if (object.value("status", std::string()) == "error") {
    completed = generation;
    if (message)
      message("SBS: " + object.value("error", std::string("Unknown error")));
    return;
  }
  if (action == "convert") {
    completed = generation;
    std::error_code ec;
    std::filesystem::remove(temporary / (std::to_string(id) + "-input.png"),
                            ec);
    if (object.value("status", std::string()) == "skipped")
      return;
    auto output = std::filesystem::path(object.value("output", std::string()));
    if (!currentOutput.empty() && currentOutput != output)
      std::filesystem::remove(currentOutput, ec);
    currentOutput = output;
    outputGeneration = generation;
    outputPixels = readImage(output).rgba;
    if (message)
      message(object.value("depth_bypassed", false)
                  ? "Existing SBS image — depth conversion skipped"
                  : "SBS image ready");
  } else if (action == "probe") {
    if (connected && !was) {
      ++generation;
    }
  } else if (action == "present" && message)
    message(connected ? "SBS image displayed in XR" : "Headset disconnected");
}
void StereoController::pump() {
  try {
    if (controls)
      controls->poll([this](const std::string &value) {
        if (value == "Close stereo view")
          setMode("off");
        else if (commandRequested)
          commandRequested(value);
      });
    worker.pump();
    dispatch();
  } catch (const std::exception &e) {
    busy = false;
    completed = generation;
    if (message)
      message(std::string("SBS: ") + e.what());
  }
}
