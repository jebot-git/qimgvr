// SPDX-License-Identifier: GPL-3.0-or-later
#include "image.h"
#include <exiv2/exiv2.hpp>
#include <fstream>
#include <iostream>
#include <opencv2/imgcodecs.hpp>
#include <unistd.h>
void require(bool ok, const char *text) {
  if (!ok)
    throw std::runtime_error(text);
}
int main() try {
  char name[] = "/tmp/qimgvr-fltk-images-XXXXXX";
  auto path = mkdtemp(name);
  require(path, "Temporary image directory");
  std::filesystem::path root = path;
  struct Cleanup {
    std::filesystem::path p;
    ~Cleanup() { std::filesystem::remove_all(p); }
  } cleanup{root};
  cv::Mat input(20, 40, CV_8UC4, cv::Scalar(12, 34, 56, 127));
  input.at<cv::Vec4b>(0, 0) = {1, 2, 3, 0};
  saveImage(root / "source.png", input);
  auto decoded = readImage(root / "source.png");
  require(decoded.rgba.rows == 20 && decoded.rgba.cols == 40 &&
              cv::norm(decoded.rgba, input, cv::NORM_INF) == 0,
          "PNG preserves straight RGBA including hidden pixels");
  saveImage(root / "source.webp", input);
  auto webp = readImage(root / "source.webp");
  require(webp.rgba.rows == 20 && webp.rgba.cols == 40,
          "OpenCV restores WebP decoding");
  auto rotated = orient(input, 6);
  require(rotated.cols == 20 && rotated.rows == 40 &&
              rotated.at<cv::Vec4b>(0, 19) == input.at<cv::Vec4b>(0, 0),
          "EXIF orientation rotates pixels correctly");
  saveImage(root / "source.jpg", input);
  auto exif = Exiv2::ImageFactory::open((root / "source.jpg").string());
  exif->readMetadata();
  exif->exifData()["Exif.Image.Orientation"] = uint16_t(6);
  exif->exifData()["Exif.Image.Artist"] = "FLTK fixture";
  exif->writeMetadata();
  auto jpeg = readImage(root / "source.jpg");
  require(jpeg.rgba.cols == 20 && jpeg.rgba.rows == 40,
          "JPEG EXIF orientation is applied at decode");
  saveImage(root / "edited.jpg", jpeg.rgba, root / "source.jpg");
  auto edited = Exiv2::ImageFactory::open((root / "edited.jpg").string());
  edited->readMetadata();
  require(edited->exifData()["Exif.Image.Orientation"].toInt64() == 1 &&
              edited->exifData()["Exif.Image.Artist"].toString() ==
                  "FLTK fixture",
          "Saving normalized edits retains metadata and resets orientation");
  std::ofstream(root / "source.svg")
      << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"40\" "
         "height=\"20\"><rect width=\"40\" height=\"20\" "
         "fill=\"#ff0000\"/></svg>";
  auto svg = readImage(root / "source.svg");
  require(svg.rgba.cols == 40 && svg.rgba.rows == 20 &&
              svg.rgba.at<cv::Vec4b>(10, 20)[0] == 255,
          "SVG is rasterized before pixel access");
  require(imageExtension("a.WEBP") && videoExtension("a.MKV") &&
              !videoExtension("a.bin"),
          "Media classification does not launch arbitrary failures as video");
  std::cout
      << "PASS: alpha-preserving image decode/save, WebP, EXIF orientation, "
         "metadata-preserving edits and media classification\n";
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
