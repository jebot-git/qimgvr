// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <filesystem>
#include <opencv2/core.hpp>
#include <string>
namespace qimgvr {
struct GltfExportResult {
  size_t vertices = 0, triangles = 0;
  double matchedFraction = 0;
  bool flatFallback = false;
  std::string summary() const;
};
// Input is the normalized, left-eye-first full SBS RGBA image already rendered
// by the stereo worker. Output is a self-contained glTF 2.0 .glb or .gltf.
GltfExportResult exportStereoGltf(const cv::Mat &rgba,
                                const std::filesystem::path &destination,
                                const std::string &sourceName = {});
}
