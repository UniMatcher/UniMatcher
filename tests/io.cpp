#include <unimatcher/io/gds.hpp>
#include <unimatcher/io/signed_edges.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace io = unimatcher::io;

void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

using EdgeRecord = std::tuple<int, int, int, int, int>;
std::vector<EdgeRecord> records(const io::Polygons& polygons) {
  const auto data = io::to_signed_edges(polygons);
  std::vector<EdgeRecord> result;
  for (std::size_t i = 0; i < data.edges.size(); ++i) {
    const auto e = data.edges[i];
    result.emplace_back(e.x, e.lo, e.hi, e.sign, data.polygon_xmax.at(i));
  }
  std::sort(result.begin(), result.end());
  return result;
}

int main() try {
  const auto fixtures = std::filesystem::path(__FILE__).parent_path() / "fixtures";
  const auto layout = io::read_layout_txt(fixtures / "layout.txt");
  check(layout.layers.size() == 2 && layout.layers[1].name == "layer2", "TXT layer identity");
  check(layout.layers[1].polygons.empty() && layout.layers[1].discarded_diagonal_polygons == 1,
        "filtered layout layer");
  const std::vector<EdgeRecord> expected{{-2, 0, 4, 1, 8}, {8, 0, 4, -1, 8}};
  check(records(layout.layers[0].polygons) == expected, "TXT winding or owner xmax");

  io::GdsOptions options;
  options.layer = 3;
  options.datatype = 4;
  options.marker_layer = 9;
  options.marker_datatype = 1;
  options.require_marker = true;
  const auto gds = io::read_gds(fixtures / "layout.gds", options);
  check(std::abs(gds.dbu_nm - 2) < 1e-12 && gds.marker &&
        gds.marker->xmin == -5 && gds.marker->ymax == 15, "GDS units or marker");
  check(records(gds.polygons) == expected, "GDS selected layer/datatype or coordinates");

  bool rejected_trailer = false;
  try {
    io::read_gds(fixtures / "nonzero-trailer.gds", options);
  } catch (const std::runtime_error& error) {
    rejected_trailer = std::string(error.what()).find("nonzero data after ENDLIB") != std::string::npos;
  }
  check(rejected_trailer, "GDS with nonzero trailing data accepted or misdiagnosed");

  auto invalid = layout.layers[0].polygons;
  invalid.offsets.back()++;
  try {
    records(invalid);
    throw std::runtime_error("invalid polygon offset accepted");
  } catch (const std::invalid_argument&) {}

  const auto flexible = io::read_patterns_txt(fixtures / "flexible.txt");
  const auto& layer = flexible.at(0).layers.at(0);
  const auto contours = io::to_fuzzy_contours(layer);
  check(contours.at(0).nominal.at(1).x == 3 && contours.at(0).alternate.at(1).x == 4,
        "lost vertex correspondence in fuzzy contour");
  try {
    io::to_signed_edges(layer);
    throw std::runtime_error("fuzzy template silently accepted as exact");
  } catch (const std::invalid_argument&) {}

  const auto patterns = io::read_patterns_txt(fixtures / "multilayer.txt");
  check(patterns.size() == 2 && patterns[0].layers.size() == 2 &&
        patterns[0].layers[1].contours.empty() && patterns[1].name == "pattern2" &&
        patterns[1].marker.ymin == -2, "multilayer patterns or empty layer");
  return 0;
} catch (const std::exception& e) {
  std::cerr << e.what() << '\n';
  return 1;
}
