#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace unimatcher::io {

struct Point {
  std::int32_t x = 0, y = 0;
  bool operator==(const Point& other) const { return x == other.x && y == other.y; }
  bool operator!=(const Point& other) const { return !(*this == other); }
};
struct Box { std::int32_t xmin = 0, ymin = 0, xmax = 0, ymax = 0; };
struct Polygons {
  std::vector<Point> vertices;
  // N+1 entries, initially {0}. Each retained polygon includes its closing point.
  std::vector<std::uint64_t> offsets{0};
  std::vector<Box> boxes;
  // +1 for positive signed area (CCW), -1 for negative signed area (CW).
  std::vector<std::int8_t> orientation;
  std::size_t size() const { return boxes.size(); }
  bool empty() const { return boxes.empty(); }
};

}  // namespace unimatcher::io
