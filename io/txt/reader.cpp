#include <unimatcher/io/txt.hpp>

#include <algorithm>
#include <charconv>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace unimatcher::io {
namespace {
bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
std::string_view trim(std::string_view s) {
  while (!s.empty() && space(s.front())) s.remove_prefix(1);
  while (!s.empty() && space(s.back())) s.remove_suffix(1);
  return s;
}
[[noreturn]] void fail(const std::filesystem::path& path, std::uint64_t line,
                       const std::string& reason) {
  throw std::runtime_error(path.string() + ":" + std::to_string(line) + ": " + reason);
}

void parse_polygon(std::string_view line, const std::filesystem::path& path,
                   std::uint64_t lineno, std::vector<Point>& points) {
  points.clear();
  const char* p = line.data();
  const char* end = p + line.size();
  auto whitespace = [&] { while (p != end && space(*p)) ++p; };
  auto token = [&](char expected) {
    whitespace();
    if (p == end || *p != expected)
      fail(path, lineno, std::string("expected '") + expected + "' in polygon");
    ++p;
  };
  auto coordinate = [&] {
    whitespace();
    // from_chars accepts a leading minus but not plus; permit the latter
    // explicitly, while rejecting a doubled sign such as '+-1'.
    bool plus = p != end && *p == '+';
    if (plus) ++p;
    if (p == end || (plus && *p == '-')) fail(path, lineno, "invalid signed coordinate");
    std::int32_t value = 0;
    const auto result = std::from_chars(p, end, value);
    if (result.ec == std::errc::result_out_of_range)
      fail(path, lineno, "coordinate outside signed int32 range");
    if (result.ec != std::errc()) fail(path, lineno, "expected integer coordinate");
    p = result.ptr;
    return value;
  };
  for (;;) {
    token('(');
    const auto x = coordinate();
    token(',');
    const auto y = coordinate();
    token(')');
    points.push_back({x, y});
    whitespace();
    if (p == end) break;
    token(',');
  }
  if (points.size() > 1 && points.front() == points.back()) points.pop_back();
}

struct Geometry {
  Box box{};
  __int128 area2 = 0;
  bool diagonal = false;
};
Geometry geometry(const std::vector<Point>& points) {
  Geometry g;
  if (points.empty()) return g;
  g.box = {points[0].x, points[0].y, points[0].x, points[0].y};
  for (std::size_t i = 0; i < points.size(); ++i) {
    const auto a = points[i], b = points[(i + 1) % points.size()];
    g.box.xmin = std::min(g.box.xmin, a.x); g.box.xmax = std::max(g.box.xmax, a.x);
    g.box.ymin = std::min(g.box.ymin, a.y); g.box.ymax = std::max(g.box.ymax, a.y);
    g.diagonal |= a.x != b.x && a.y != b.y;
    g.area2 += __int128(a.x) * b.y - __int128(b.x) * a.y;
  }
  return g;
}
void validate(const std::vector<Point>& points, const Geometry& g,
              const std::filesystem::path& path, std::uint64_t line) {
  if (points.size() < 4) fail(path, line, "contour needs at least four vertices");
  if (g.diagonal) fail(path, line, "diagonal template/marker contour is unsupported");
  if (g.area2 == 0) fail(path, line, "contour has zero signed area");
}
Box marker_box(const std::vector<Point>& points, const Geometry& g,
               const std::filesystem::path& path, std::uint64_t line) {
  validate(points, g, path, line);
  const auto& b = g.box;
  const __int128 expected = 2 * (__int128(b.xmax) - b.xmin) * (__int128(b.ymax) - b.ymin);
  if (expected <= 0 || (g.area2 < 0 ? -g.area2 : g.area2) != expected)
    fail(path, line, "marker must fill its positive-area rectangular bounding box");
  for (std::size_t i = 0; i < points.size(); ++i) {
    const auto p = points[i], q = points[(i + 1) % points.size()];
    const bool vertical = p.x == q.x && (p.x == b.xmin || p.x == b.xmax);
    const bool horizontal = p.y == q.y && (p.y == b.ymin || p.y == b.ymax);
    if (!vertical && !horizontal) fail(path, line, "marker has an interior segment");
  }
  return b;
}

bool numbered(std::string_view name, std::string_view prefix) {
  return name.size() > prefix.size() && name.substr(0, prefix.size()) == prefix &&
      std::all_of(name.begin() + prefix.size(), name.end(), [](char c) { return c >= '0' && c <= '9'; });
}

template <typename Handler>
void read_lines(const std::filesystem::path& path, Handler handle) {
  std::ifstream input(path);
  if (!input) fail(path, 0, "cannot open input");
  std::string text;
  std::size_t number = 0;
  while (std::getline(input, text)) {
    ++number;
    const auto line = trim(text);
    if (!line.empty()) handle(line, number);
  }
  if (input.bad()) fail(path, number, "error reading input");
}

bool inside(Point p, Box b) {
  return b.xmin <= p.x && p.x <= b.xmax && b.ymin <= p.y && p.y <= b.ymax;
}
}  // namespace

TxtLayout read_layout_txt(const std::filesystem::path& path) {
  TxtLayout result;
  std::unordered_set<std::string> names;
  std::vector<Point> points;
  bool unlabelled = false;
  read_lines(path, [&](std::string_view line, std::size_t n) {
    if (line.back() == ':') {
      auto name = trim(line.substr(0, line.size() - 1));
      if (unlabelled || !numbered(name, "layer")) fail(path, n, "unexpected layout section");
      if (!names.emplace(name).second) fail(path, n, "duplicate layout layer");
      result.layers.push_back({std::string(name), {}});
      return;
    }
    if (result.layers.empty()) {
      result.layers.push_back({"layer1", {}});
      unlabelled = true;
    }
    auto& layer = result.layers.back();
    parse_polygon(line, path, n, points);
    const auto g = geometry(points);
    if (g.diagonal) {
      ++layer.discarded_diagonal_polygons;
      return;
    }
    validate(points, g, path, n);
    auto& p = layer.polygons;
    p.vertices.insert(p.vertices.end(), points.begin(), points.end());
    p.vertices.push_back(points.front());
    p.offsets.push_back(p.vertices.size());
    p.boxes.push_back(g.box);
    p.orientation.push_back(g.area2 > 0 ? 1 : -1);
  });
  if (result.layers.empty()) fail(path, 0, "empty layout");
  return result;
}

std::vector<TxtPattern> read_patterns_txt(const std::filesystem::path& path) {
  std::vector<TxtPattern> result;
  std::unordered_set<std::string> names, layer_names;
  TxtPattern entry;
  bool single_layer = false;
  enum class Section { None, Layer, Marker, Done };
  Section section = Section::None;
  std::size_t last_line = 0;

  auto finish = [&] {
    if (entry.name.empty()) return;
    if (section != Section::Done) fail(path, last_line, "pattern requires one shared marker");
    for (const auto& layer : entry.layers) {
      for (const auto& contour : layer.contours) {
        auto check = [&](const std::vector<Point>& points) {
          for (Point p : points) {
            if (!inside(p, entry.marker)) fail(path, last_line, "pattern vertex outside shared marker");
          }
        };
        check(contour.nominal);
        if (contour.alternate) check(*contour.alternate);
      }
    }
    result.push_back(std::move(entry));
    entry = {};
    layer_names.clear();
    section = Section::None;
  };

  std::vector<Point> points;
  read_lines(path, [&](std::string_view line, std::size_t n) {
    last_line = n;
    if (line.back() == ':') {
      const auto name = trim(line.substr(0, line.size() - 1));
      if (name == "pattern" || numbered(name, "pattern")) {
        finish();
        if (!names.emplace(name).second) fail(path, n, "duplicate pattern section");
        single_layer = name == "pattern";
        if (!result.empty() && (single_layer || result.front().name == "pattern"))
          fail(path, n, "cannot mix single-pattern and multilayer library formats");
        entry.name = name;
        if (single_layer) {
          entry.layers.push_back({"layer1", {}});
          section = Section::Layer;
        }
      } else if (numbered(name, "layer")) {
        if (single_layer || entry.name.empty() || section == Section::Marker || section == Section::Done)
          fail(path, n, "misplaced layer section");
        if (!layer_names.emplace(name).second) fail(path, n, "duplicate pattern layer");
        entry.layers.push_back({std::string(name), {}});
        section = Section::Layer;
      } else if (name == "marker") {
        if (section != Section::Layer || (single_layer && entry.layers.front().contours.empty()))
          fail(path, n, "misplaced marker section");
        section = Section::Marker;
      } else {
        fail(path, n, "unrecognized pattern section");
      }
      return;
    }
    if (section != Section::Layer && section != Section::Marker) fail(path, n, "unexpected polygon");
    const bool alternate = line.front() == '[';
    if (alternate) {
      if (!single_layer || section != Section::Layer || line.back() != ']')
        fail(path, n, "misplaced or incomplete alternate contour");
      line = trim(line.substr(1, line.size() - 2));
    }
    parse_polygon(line, path, n, points);
    const auto g = geometry(points);
    if (section == Section::Marker) {
      entry.marker = marker_box(points, g, path, n);
      section = Section::Done;
      return;
    }
    validate(points, g, path, n);
    auto& contours = entry.layers.back().contours;
    if (alternate) {
      if (contours.empty() || contours.back().alternate) fail(path, n, "alternate needs a nominal contour");
      const auto& nominal = contours.back().nominal;
      if (points.size() != nominal.size() || (g.area2 > 0) != (geometry(nominal).area2 > 0))
        fail(path, n, "alternate must preserve vertex count and winding");
      contours.back().alternate = points;
    } else {
      contours.push_back({points, std::nullopt});
    }
  });
  finish();
  if (result.empty()) fail(path, 0, "empty pattern library");
  return result;
}
}  // namespace unimatcher::io
