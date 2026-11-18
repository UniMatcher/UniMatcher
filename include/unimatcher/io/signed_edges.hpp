#pragma once

#include <unimatcher/core.hpp>
#include <unimatcher/io/txt.hpp>
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace unimatcher::io {

struct SignedEdges {
  std::vector<unimatcher::Edge> edges;
  // Rightmost x of the source polygon, one value per edge in the same order.
  std::vector<std::int32_t> polygon_xmax;
};

// Extract raw vertical edges from checked Manhattan contours in polygon and
// vertex order. The paper uses 64 OpenMP threads for layouts and one thread
// for templates. Set include_polygon_xmax to false when only edges are needed.
// The bounded GPU union requires one parent-polygon xmax per edge.
SignedEdges extract_signed_edges(const Polygons& polygons, int threads = 64,
                                 bool include_polygon_xmax = true);

inline unimatcher::Box to_box(Box b) {
  return {b.xmin, b.ymin, b.xmax, b.ymax};
}

namespace detail {
inline void append_vertical_edge(SignedEdges& result, Point a, Point b, int winding,
                                 std::int32_t xmax) {
  if (a.x != b.x || a.y == b.y) return;
  result.edges.push_back({a.x, std::min(a.y, b.y), std::max(a.y, b.y),
                          a.y > b.y ? winding : -winding});
  result.polygon_xmax.push_back(xmax);
}
}  // namespace detail

// Converts the checked, explicitly closed layout contours from either reader.
inline SignedEdges to_signed_edges(const Polygons& polygons) {
  if (polygons.offsets.empty() || polygons.offsets.size() - 1 != polygons.size() ||
      polygons.orientation.size() != polygons.size() || polygons.offsets.front() != 0 ||
      polygons.offsets.back() != polygons.vertices.size())
    throw std::invalid_argument("inconsistent polygon buffers");
  SignedEdges result;
  for (std::size_t i = 0; i < polygons.size(); ++i) {
    const auto begin = polygons.offsets.at(i);
    const auto end = polygons.offsets.at(i + 1);
    if (begin >= end || end - begin < 5 || end > polygons.vertices.size() ||
        polygons.vertices[begin] != polygons.vertices[end - 1])
      throw std::invalid_argument("invalid closed polygon range");
    const auto winding = polygons.orientation[i];
    if (winding != 1 && winding != -1)
      throw std::invalid_argument("invalid polygon winding");
    for (auto j = begin + 1; j < end; ++j)
      detail::append_vertical_edge(result, polygons.vertices[j - 1], polygons.vertices[j],
                                   winding, polygons.boxes[i].xmax);
  }
  return result;
}

// Converts exact templates only. In a fuzzy template the bracketed alternate
// bounds a moving edge. A caller must handle this constraint explicitly.
inline SignedEdges to_signed_edges(const PatternLayer& layer) {
  SignedEdges result;
  for (const auto& contour : layer.contours) {
    if (contour.alternate)
      throw std::invalid_argument("flexible template requires a constrained fuzzy matcher");
    const auto& points = contour.nominal;
    if (points.size() < 4) throw std::invalid_argument("invalid template contour");
    std::int32_t xmax = points.front().x;
    __int128 area = 0;
    for (std::size_t i = 0; i < points.size(); ++i) {
      const auto a = points[i], b = points[(i + 1) % points.size()];
      xmax = std::max(xmax, a.x);
      area += __int128(a.x) * b.y - __int128(b.x) * a.y;
    }
    if (area == 0) throw std::invalid_argument("zero-area template contour");
    for (std::size_t i = 0; i < points.size(); ++i)
      detail::append_vertical_edge(result, points[i], points[(i + 1) % points.size()],
                                   area > 0 ? 1 : -1, xmax);
  }
  return result;
}

// Preserves vertex correspondence between nominal and extreme contours for
// prepare_fuzzy_pattern. Fixed contours have an empty alternate.
inline std::vector<unimatcher::FuzzyContour> to_fuzzy_contours(const PatternLayer& layer) {
  auto convert = [](const std::vector<Point>& source) {
    std::vector<unimatcher::Point> points;
    points.reserve(source.size());
    for (auto p : source) points.push_back({p.x, p.y});
    return points;
  };
  std::vector<unimatcher::FuzzyContour> contours;
  contours.reserve(layer.contours.size());
  for (const auto& input : layer.contours) {
    unimatcher::FuzzyContour contour{convert(input.nominal), {}};
    if (input.alternate) contour.alternate = convert(*input.alternate);
    contours.push_back(std::move(contour));
  }
  return contours;
}

}  // namespace unimatcher::io
