#include <unimatcher/io/signed_edges.hpp>

#include <algorithm>
#include <climits>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace unimatcher::io {

SignedEdges extract_signed_edges(const Polygons& polygons, int threads,
                                 bool include_polygon_xmax) {
  if (threads < 1)
    throw std::invalid_argument("edge extraction requires a positive thread count");
  const std::size_t n = polygons.size();
  if (n > std::size_t(LLONG_MAX) || polygons.offsets.size() != n + 1 ||
      polygons.orientation.size() != n || polygons.offsets.front() != 0 ||
      polygons.offsets.back() != polygons.vertices.size())
    throw std::invalid_argument("inconsistent polygon buffers");

  // Before the scan, offsets[i + 1] holds the vertical-edge count of polygon i.
  std::vector<std::uint64_t> offsets(n + 1);
  bool invalid = false;
  #pragma omp parallel for num_threads(threads) schedule(static) reduction(||:invalid)
  for (long long i = 0; i < static_cast<long long>(n); ++i) {
    const auto begin = polygons.offsets[i];
    const auto end = polygons.offsets[i + 1];
    if (begin > end || end > polygons.vertices.size() || end - begin < 5 ||
        polygons.vertices[begin] != polygons.vertices[end - 1] ||
        (polygons.orientation[i] != 1 && polygons.orientation[i] != -1)) {
      invalid = true;
      continue;
    }
    std::uint64_t count = 0;
    for (auto j = begin; j + 1 < end; ++j) {
      const auto a = polygons.vertices[j], b = polygons.vertices[j + 1];
      if (a.x != b.x && a.y != b.y) invalid = true;
      count += a.x == b.x && a.y != b.y;
    }
    offsets[i + 1] = count;
  }
  if (invalid)
    throw std::invalid_argument("expected explicitly closed, oriented Manhattan polygons");
  for (std::size_t i = 0; i < n; ++i) {
    offsets[i + 1] += offsets[i];
    if (offsets[i + 1] > (1ULL << 30))
      throw std::length_error("industrial raw edge limit exceeded");
  }

  SignedEdges result;
  result.edges.resize(offsets.back());
  if (include_polygon_xmax) result.polygon_xmax.resize(offsets.back());
  #pragma omp parallel for num_threads(threads) schedule(static)
  for (long long i = 0; i < static_cast<long long>(n); ++i) {
    auto output = offsets[i];
    for (auto j = polygons.offsets[i]; j + 1 < polygons.offsets[i + 1]; ++j) {
      const auto a = polygons.vertices[j], b = polygons.vertices[j + 1];
      if (a.x != b.x || a.y == b.y) continue;
      const int sign = a.y > b.y ? polygons.orientation[i] : -polygons.orientation[i];
      result.edges[output] = {a.x, std::min(a.y, b.y), std::max(a.y, b.y), sign};
      if (include_polygon_xmax) result.polygon_xmax[output] = polygons.boxes[i].xmax;
      ++output;
    }
  }
  return result;
}

}  // namespace unimatcher::io
