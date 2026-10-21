// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <filesystem>
#include <map>
#include <opencv2/core.hpp>
#include <string>
struct ImageData {
  cv::Mat rgba;
  std::map<std::string, std::string> metadata;
  std::string description;
};
ImageData readImage(const std::filesystem::path &);
void saveImage(const std::filesystem::path &, const cv::Mat &,
               const std::filesystem::path &metadataSource = {});
cv::Mat orient(const cv::Mat &, int orientation);
bool imageExtension(const std::filesystem::path &);
bool videoExtension(const std::filesystem::path &);
