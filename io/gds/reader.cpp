#include <unimatcher/io/gds.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fcntl.h>
#include <limits>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace unimatcher::io {
namespace {
using Clock = std::chrono::steady_clock;
double ms(Clock::time_point begin) {
  return std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
}
unsigned u16(const unsigned char* p) { return unsigned(p[0]) * 256 + p[1]; }
std::int32_t i32(const unsigned char* p) {
  return static_cast<std::int32_t>((std::uint32_t(p[0]) << 24) |
      (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3]);
}
double real8(const unsigned char* p) {
  std::uint64_t mantissa = 0;
  for (int i = 1; i < 8; ++i) mantissa = (mantissa << 8) | p[i];
  return (p[0] & 128 ? -1 : 1) *
      std::ldexp(double(mantissa), 4 * (int(p[0] & 127) - 64) - 56);
}
Point point(const unsigned char* p) { return {i32(p), i32(p + 4)}; }

class Mapping {
 public:
  const unsigned char* data = nullptr;
  std::size_t size = 0;
  explicit Mapping(const std::filesystem::path& path) {
    const int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) throw std::runtime_error("cannot open GDS: " + path.string());
    struct stat st {};
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
        std::uintmax_t(st.st_size) > std::numeric_limits<std::size_t>::max()) {
      close(fd);
      throw std::runtime_error("GDS must be a readable nonempty regular file: " + path.string());
    }
    size = static_cast<std::size_t>(st.st_size);
    void* address = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (address == MAP_FAILED) throw std::runtime_error("cannot mmap GDS: " + path.string());
    data = static_cast<const unsigned char*>(address);
    madvise(const_cast<unsigned char*>(data), size, MADV_SEQUENTIAL);
  }
  Mapping(const Mapping&) = delete;
  Mapping& operator=(const Mapping&) = delete;
  void release() {
    if (data) munmap(const_cast<unsigned char*>(data), size);
    data = nullptr;
  }
  ~Mapping() { release(); }
};

bool selected(int layer, int datatype, const GdsOptions& o) {
  return layer == o.layer && datatype == o.datatype;
}
bool is_marker(int layer, int datatype, const GdsOptions& o) {
  return o.marker_layer >= 0 && layer == o.marker_layer && datatype == o.marker_datatype;
}

// First pass validates record structure and determines exact upper allocation
// bounds. The second pass decodes only selected BOUNDARY coordinate records.
void scan(const Mapping& file, const GdsOptions& o, GdsData& result,
          const std::filesystem::path& path) {
  bool header = false, library = false, libname = false, units = false;
  bool cell = false, cell_name = false, cell_closed = false;
  unsigned cells = 0;
  int element = -1, layer = -1, datatype = -1;
  bool xy = false;
  std::size_t coordinate_count = 0;
  for (std::size_t off = 0; off < file.size;) {
    auto fail = [&](const std::string& reason) {
      throw std::runtime_error(path.string() + ": " + reason + " at byte " + std::to_string(off));
    };
    if (file.size - off < 4) fail("truncated record header");
    const auto* p = file.data + off;
    const unsigned length = u16(p), kind = p[2], dtype = p[3];
    if (length < 4 || (length & 1) || length > file.size - off) fail("invalid record length");
    const unsigned bytes = length - 4;
    const auto* payload = p + 4;
    ++result.stats.records;
    auto fixed = [&](unsigned expected_dtype, unsigned expected_bytes) {
      if (dtype != expected_dtype || bytes != expected_bytes) fail("invalid record type/size");
    };
    if (!header && kind != 0) fail("missing HEADER");
    switch (kind) {
      case 0:  // HEADER
        fixed(2, 2);
        if (header) fail("duplicate HEADER");
        header = true;
        break;
      case 1:  // BGNLIB
        fixed(2, 24);
        if (library || cells) fail("duplicate or misplaced BGNLIB");
        library = true;
        break;
      case 2:  // LIBNAME
        if (!library || libname || cells || dtype != 6 || bytes == 0) fail("invalid LIBNAME");
        libname = true;
        break;
      case 3:  // UNITS
        fixed(5, 16);
        if (!library || units || cells) fail("duplicate or misplaced UNITS");
        result.dbu_nm = real8(payload + 8) * 1e9;
        if (!std::isfinite(real8(payload)) || real8(payload) <= 0 ||
            !std::isfinite(result.dbu_nm) || result.dbu_nm <= 0) fail("invalid UNITS");
        units = true;
        break;
      case 5:  // BGNSTR
        fixed(2, 24);
        if (!library || !libname || !units || cell || ++cells != 1) fail("expected exactly one flat cell");
        cell = true;
        break;
      case 6:  // STRNAME
        if (!cell || cell_name || element >= 0 || dtype != 6 || bytes == 0) fail("invalid STRNAME");
        cell_name = true;
        break;
      case 7:  // ENDSTR
        fixed(0, 0);
        if (!cell || !cell_name || element >= 0) fail("invalid ENDSTR");
        cell = false;
        cell_closed = true;
        break;
      case 8: case 12:  // BOUNDARY / TEXT
        fixed(0, 0);
        if (!cell || !cell_name || element >= 0) fail("misplaced or nested element");
        element = int(kind);
        layer = datatype = -1;
        xy = false;
        coordinate_count = 0;
        break;
      case 9: case 10: case 11: case 21: case 45:
        fail("unsupported PATH/reference/NODE/BOX (input must be flat BOUNDARY geometry)");
        break;
      case 13:  // LAYER
        fixed(2, 2);
        if (element < 0 || layer >= 0) fail("invalid or duplicate LAYER");
        layer = int(u16(payload));
        break;
      case 14:  // DATATYPE
        fixed(2, 2);
        if (element != 8 || datatype >= 0) fail("invalid or duplicate DATATYPE");
        datatype = int(u16(payload));
        break;
      case 16:  // XY
        if (element < 0 || xy || dtype != 3 || bytes % 8 ||
            (element == 8 ? bytes < 32 : bytes != 8)) fail("invalid or duplicate XY");
        xy = true;
        coordinate_count = bytes / 8;
        break;
      case 17:  // ENDEL
        fixed(0, 0);
        if (element < 0 || !xy || layer < 0 || (element == 8 && datatype < 0)) fail("incomplete element");
        if (element == 8) {
          ++result.stats.boundary_polygons;
          if (selected(layer, datatype, o)) {
            ++result.stats.selected_polygons;
            if (coordinate_count > std::numeric_limits<std::uint64_t>::max() - result.stats.selected_vertices)
              fail("coordinate count overflow");
            result.stats.selected_vertices += coordinate_count;
          } else if (!is_marker(layer, datatype, o)) {
            ++result.stats.ignored_other_layer_polygons;
          }
        } else {
          ++result.stats.text_elements;
        }
        element = -1;
        break;
      case 4:  // ENDLIB
        fixed(0, 0);
        if (!cell_closed || cell || element >= 0) fail("invalid ENDLIB");
        // Some stream writers zero-fill the last physical block after ENDLIB.
        if (!std::all_of(p + length, file.data + file.size, [](unsigned char b) { return b == 0; }))
          fail("nonzero data after ENDLIB");
        return;
      // Element metadata that does not affect a BOUNDARY's integer geometry.
      case 38:  // ELFLAGS
        fixed(1, 2);
        if (element < 0) fail("ELFLAGS outside element");
        break;
      case 47:  // PLEX
        fixed(3, 4);
        if (element < 0) fail("PLEX outside element");
        break;
      case 43:  // PROPATTR
        fixed(2, 2);
        if (element < 0) fail("PROPATTR outside element");
        break;
      case 44:  // PROPVALUE
        if (element < 0 || dtype != 6) fail("invalid PROPVALUE");
        break;
      case 22:  // TEXTTYPE
        fixed(2, 2);
        if (element != 12) fail("TEXTTYPE outside TEXT");
        break;
      case 23: case 26:  // PRESENTATION / STRANS, ignored text metadata
        fixed(1, 2);
        if (element != 12) fail("text metadata outside TEXT");
        break;
      case 27: case 28:  // MAG / ANGLE
        fixed(5, 8);
        if (element != 12) fail("text transform outside TEXT");
        break;
      case 25:  // STRING
        if (element != 12 || dtype != 6) fail("STRING outside TEXT");
        break;
      case 15:  // WIDTH
        fixed(3, 4);
        if (element != 12) fail("WIDTH outside TEXT");
        break;
      case 33:  // PATHTYPE (only permitted on ignored TEXT)
        fixed(2, 2);
        if (element != 12) fail("PATHTYPE outside TEXT");
        break;
      case 52:  // STRCLASS
        fixed(1, 2);
        if (!cell || element >= 0) fail("misplaced STRCLASS");
        break;
      default:
        fail("unsupported GDS record " + std::to_string(kind));
    }
    off += length;
  }
  throw std::runtime_error(path.string() + ": incomplete flat GDS");
}

void decode_polygon(const unsigned char* data, std::size_t count, bool marker,
                    GdsData& out, const std::filesystem::path& path) {
  auto fail = [&](const char* reason) { throw std::runtime_error(path.string() + ": " + reason); };
  const Point first = point(data);
  const bool closed = first == point(data + (count - 1) * 8);
  const std::size_t n = count - (closed ? 1 : 0);
  Box box{first.x, first.y, first.x, first.y};
  bool diagonal = false, zero = false;
  __int128 area2 = 0;
  std::uint64_t vertical = 0;
  Point a = first;
  for (std::size_t i = 0; i < n; ++i) {
    const Point b = i + 1 < n ? point(data + (i + 1) * 8) : first;
    box.xmin = std::min(box.xmin, a.x); box.xmax = std::max(box.xmax, a.x);
    box.ymin = std::min(box.ymin, a.y); box.ymax = std::max(box.ymax, a.y);
    diagonal |= a.x != b.x && a.y != b.y;
    zero |= a == b;
    vertical += a.x == b.x && a.y != b.y;
    area2 += __int128(a.x) * b.y - __int128(b.x) * a.y;
    a = b;
  }
  if (!marker && diagonal) {
    ++out.stats.discarded_diagonal_polygons;
    return;
  }
  if (!closed || n < 4) fail("selected polygon is not an explicitly closed contour");
  if (!area2) fail("selected polygon has zero signed area");
  if (marker) {
    if (out.marker) fail("multiple selected markers");
    if (diagonal || box.xmin == box.xmax || box.ymin == box.ymax)
      fail("marker must be a positive-size axis-aligned rectangle");
    const __int128 expected = 2 * (__int128(box.xmax) - box.xmin) *
        (__int128(box.ymax) - box.ymin);
    if ((area2 < 0 ? -area2 : area2) != expected) fail("marker does not fill its rectangular bounding box");
    // Every segment must be on the bounding-box perimeter. This permits
    // duplicate/collinear marker points without accepting a concave marker.
    for (std::size_t i = 0; i < n; ++i) {
      const Point p = point(data + i * 8), q = point(data + (i + 1) * 8);
      const bool on_vertical = p.x == q.x && (p.x == box.xmin || p.x == box.xmax);
      const bool on_horizontal = p.y == q.y && (p.y == box.ymin || p.y == box.ymax);
      if (!on_vertical && !on_horizontal) fail("marker has an interior segment");
    }
    out.marker = box;
    return;
  }
  auto& polygons = out.polygons;
  for (std::size_t i = 0; i < count; ++i) polygons.vertices.push_back(point(data + 8 * i));
  polygons.offsets.push_back(polygons.vertices.size());
  polygons.boxes.push_back(box);
  polygons.orientation.push_back(area2 > 0 ? 1 : -1);
  ++out.stats.retained_polygons;
  out.stats.retained_vertices += count;
  out.stats.raw_vertical_edges += vertical;
  out.stats.clockwise_polygons += area2 < 0;
  out.stats.counterclockwise_polygons += area2 > 0;
  out.stats.polygons_with_zero_length_edges += zero;
}

void decode(const Mapping& file, const GdsOptions& o, GdsData& out,
            const std::filesystem::path& path) {
  int element = -1, layer = -1, datatype = -1;
  const unsigned char* coordinates = nullptr;
  std::size_t count = 0;
  for (std::size_t off = 0; off < file.size;) {
    const auto* p = file.data + off;
    const unsigned length = u16(p), kind = p[2];
    switch (kind) {
      case 4: return;  // ENDLIB; the first pass validated any trailing padding.
      case 8: case 12:
        element = int(kind); layer = datatype = -1; coordinates = nullptr;
        break;
      case 13: layer = int(u16(p + 4)); break;
      case 14: datatype = int(u16(p + 4)); break;
      case 16:
        if (element == 8) { coordinates = p + 4; count = (length - 4) / 8; }
        break;
      case 17:
        if (element == 8 && (selected(layer, datatype, o) || is_marker(layer, datatype, o)))
          decode_polygon(coordinates, count, is_marker(layer, datatype, o), out, path);
        element = -1;
        break;
      default: break;
    }
    off += length;
  }
}
}  // namespace

GdsData read_gds(const std::filesystem::path& path, const GdsOptions& options) {
  const auto wall = Clock::now();
  auto valid_layer = [](int value) { return value >= 0 && value <= 65535; };
  if (!valid_layer(options.layer) || !valid_layer(options.datatype) ||
      !valid_layer(options.marker_datatype) ||
      (options.marker_layer != -1 && !valid_layer(options.marker_layer)) ||
      (options.require_marker && options.marker_layer < 0) ||
      (options.marker_layer == options.layer && options.marker_datatype == options.datatype))
    throw std::invalid_argument("invalid or overlapping GDS geometry/marker layer selection");
  GdsData result;
  auto started = Clock::now();
  Mapping file(path);
  result.stats.file_bytes = file.size;
  result.stats.map_ms = ms(started);
  started = Clock::now();
  scan(file, options, result, path);
  result.stats.scan_ms = ms(started);
  started = Clock::now();
  const auto polygons = result.stats.selected_polygons, vertices = result.stats.selected_vertices;
  if (vertices > result.polygons.vertices.max_size() ||
      polygons > result.polygons.boxes.max_size() ||
      polygons >= result.polygons.offsets.max_size() ||
      polygons > result.polygons.orientation.max_size())
    throw std::length_error("selected GDS geometry exceeds host vector capacity");
  result.polygons.vertices.reserve(static_cast<std::size_t>(vertices));
  result.polygons.offsets.reserve(static_cast<std::size_t>(polygons) + 1);
  result.polygons.boxes.reserve(static_cast<std::size_t>(polygons));
  result.polygons.orientation.reserve(static_cast<std::size_t>(polygons));
  result.stats.allocation_ms = ms(started);
  started = Clock::now();
  decode(file, options, result, path);
  if (options.require_marker && !result.marker)
    throw std::runtime_error(path.string() + ": required marker is missing");
  result.stats.decode_ms = ms(started);
  started = Clock::now();
  file.release();
  result.stats.unmap_ms = ms(started);
  result.stats.wall_ms = ms(wall);
  return result;
}
}  // namespace unimatcher::io
