// SPDX-License-Identifier: GPL-3.0-or-later
#include "image.h"
#include <FL/Fl_SVG_Image.H>
#include <algorithm>
#include <exiv2/exiv2.hpp>
#include <fstream>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <set>
#include <sstream>
#include <unistd.h>
#include <vector>
cv::Mat orient(const cv::Mat &input, int orientation) {
  cv::Mat output;
  switch (orientation) {
  case 2:
    cv::flip(input, output, 1);
    break;
  case 3:
    cv::rotate(input, output, cv::ROTATE_180);
    break;
  case 4:
    cv::flip(input, output, 0);
    break;
  case 5:
    cv::transpose(input, output);
    break;
  case 6:
    cv::rotate(input, output, cv::ROTATE_90_CLOCKWISE);
    break;
  case 7:
    cv::transpose(input, output);
    cv::flip(output, output, -1);
    break;
  case 8:
    cv::rotate(input, output, cv::ROTATE_90_COUNTERCLOCKWISE);
    break;
  default:
    output = input.clone();
  }
  return output;
}
ImageData readImage(const std::filesystem::path &path) {
  auto bytes = std::filesystem::file_size(path);
  if (bytes > 512 * 1024 * 1024)
    throw std::runtime_error("Image file exceeds 512 MiB");
  cv::Mat decoded;
  if (path.extension() == ".svg" || path.extension() == ".SVG") {
    Fl_SVG_Image image(path.c_str());
    image.normalize();
    if (image.fail() || !image.data() || !image.data()[0])
      throw std::runtime_error("Cannot decode SVG");
    cv::Mat rgb(image.data_h(), image.data_w(),
                image.d() == 4 ? CV_8UC4 : CV_8UC3, (void *)image.data()[0]);
    cv::cvtColor(rgb, decoded,
                 image.d() == 4 ? cv::COLOR_RGBA2BGRA : cv::COLOR_RGB2BGR);
  } else
    decoded = cv::imread(path.string(), cv::IMREAD_UNCHANGED);
  if (decoded.empty())
    throw std::runtime_error("Cannot decode image: " + path.string());
  if (decoded.total() > 100000000)
    throw std::runtime_error("Image exceeds the 100 megapixel limit");
  if (decoded.depth() == CV_16U)
    decoded.convertTo(decoded, CV_8U, 1. / 257.);
  if (decoded.depth() != CV_8U)
    throw std::runtime_error("Unsupported image sample type");
  ImageData result;
  if (decoded.channels() == 1)
    cv::cvtColor(decoded, result.rgba, cv::COLOR_GRAY2RGBA);
  else if (decoded.channels() == 3)
    cv::cvtColor(decoded, result.rgba, cv::COLOR_BGR2RGBA);
  else if (decoded.channels() == 4)
    cv::cvtColor(decoded, result.rgba, cv::COLOR_BGRA2RGBA);
  else
    throw std::runtime_error("Unsupported image channels");
  int orientation = 1;
  std::ostringstream details;
  details << path.filename().string() << "\n"
          << decoded.cols << " × " << decoded.rows << "\n"
          << bytes << " bytes\n";
  try {
    auto image = Exiv2::ImageFactory::open(path.string());
    if (image) {
      image->readMetadata();
      auto &exif = image->exifData();
      auto it = exif.findKey(Exiv2::ExifKey("Exif.Image.Orientation"));
      if (it != exif.end())
        orientation = it->toInt64();
      int count = 0;
      for (auto &entry : exif) {
        auto value = entry.toString();
        if (value.size() > 4096)
          value.resize(4096);
        result.metadata[entry.key()] = value;
        if (++count <= 100)
          details << entry.tagLabel() << ": " << value << "\n";
      }
      for (auto &entry : image->xmpData()) {
        auto value = entry.toString();
        if (value.size() < 4096)
          result.metadata[entry.key()] = value;
      }
    }
  } catch (const Exiv2::Error &) {
  }
  result.rgba = orient(result.rgba, orientation);
  result.metadata["Exif.Image.Orientation"] = "1";
  result.description = details.str();
  return result;
}
void saveImage(const std::filesystem::path &destination, const cv::Mat &rgba,
               const std::filesystem::path &metadataSource) {
  if (rgba.empty() || rgba.type() != CV_8UC4)
    throw std::runtime_error("No editable image");
  auto suffix = destination.extension().string();
  std::transform(suffix.begin(), suffix.end(), suffix.begin(), ::tolower);
  auto pattern =
      (destination.parent_path() / (".qimgvr-save-XXXXXX" + suffix)).string();
  std::vector<char> name(pattern.begin(), pattern.end());
  name.push_back(0);
  int descriptor = mkstemps(name.data(), suffix.size());
  if (descriptor < 0)
    throw std::runtime_error("Cannot create output beside the destination");
  close(descriptor);
  std::filesystem::path temporary = name.data();
  cv::Mat encoded;
  bool alpha = suffix == ".png" || suffix == ".webp" || suffix == ".tif" ||
               suffix == ".tiff";
  cv::cvtColor(rgba, encoded, alpha ? cv::COLOR_RGBA2BGRA : cv::COLOR_RGBA2BGR);
  try {
    if (!cv::imwrite(temporary.string(), encoded))
      throw std::runtime_error("Cannot encode image");
    if (!metadataSource.empty())
      try {
        auto source = Exiv2::ImageFactory::open(metadataSource.string()),
             target = Exiv2::ImageFactory::open(temporary.string());
        if (source && target) {
          source->readMetadata();
          target->setExifData(source->exifData());
          target->setIptcData(source->iptcData());
          target->setXmpData(source->xmpData());
          target->exifData()["Exif.Image.Orientation"] = uint16_t(1);
          target->exifData()["Exif.Photo.PixelXDimension"] =
              uint32_t(rgba.cols);
          target->exifData()["Exif.Photo.PixelYDimension"] =
              uint32_t(rgba.rows);
          target->writeMetadata();
        }
      } catch (const Exiv2::Error &) {
      }
    std::filesystem::rename(temporary, destination);
  } catch (...) {
    std::error_code ec;
    std::filesystem::remove(temporary, ec);
    throw;
  }
}
static std::string ext(const std::filesystem::path &p) {
  auto s = p.extension().string();
  std::transform(s.begin(), s.end(), s.begin(), ::tolower);
  return s;
}
bool imageExtension(const std::filesystem::path &p) {
  static const std::set<std::string> extensions = {
      ".svg",  ".png", ".jpg", ".jpeg", ".bmp",  ".gif", ".webp", ".tif",
      ".tiff", ".ppm", ".pgm", ".pnm",  ".avif", ".jxl", ".exr",  ".hdr"};
  return extensions.count(ext(p));
}
bool videoExtension(const std::filesystem::path &p) {
  static const std::set<std::string> extensions = {
      ".mp4", ".mkv", ".webm", ".avi", ".mov",  ".m4v", ".mpeg",
      ".mpg", ".ogv", ".ts",   ".mts", ".m2ts", ".gifv"};
  return extensions.count(ext(p));
}
