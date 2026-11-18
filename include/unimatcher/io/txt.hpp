#pragma once

#include <unimatcher/io/geometry.hpp>
#include <filesystem>
#include <optional>
#include <string>

namespace unimatcher::io {

struct LayoutLayer {
  std::string name;
  Polygons polygons;
  std::size_t discarded_diagonal_polygons = 0;
};

struct TxtLayout {
  std::vector<LayoutLayer> layers;
};

struct PatternContour {
  // Unclosed vertex lists in source order. An alternate is the extreme position
  // of the preceding nominal contour in a 2023 flexible template.
  std::vector<Point> nominal;
  std::optional<std::vector<Point>> alternate;
};

struct PatternLayer {
  std::string name;
  std::vector<PatternContour> contours;
};

struct TxtPattern {
  std::string name;
  Box marker{};
  std::vector<PatternLayer> layers;
};

// Reads 2023 unlabelled layouts and 2024 layerN sections in source order.
// Integer coordinates use contest units. Unlabelled geometry is named layer1.
// Each polygon with any diagonal segment is skipped and counted in its layer.
TxtLayout read_layout_txt(const std::filesystem::path& path);

// Reads the 2023 single-template format and 2024 multi-template libraries.
// A common rectangular marker bounds all layers of one template. Empty named
// layers are preserved. Alternate contours remain associated with their nominal.
std::vector<TxtPattern> read_patterns_txt(const std::filesystem::path& path);

}  // namespace unimatcher::io
