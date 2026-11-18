#include <unimatcher/io/gds.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <limits>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <omp.h>

namespace unimatcher::io {
namespace {
using Clock = std::chrono::steady_clock;
double ms(Clock::time_point begin) {
  return std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
}
unsigned u16(const unsigned char* p) { return unsigned(p[0]) * 256 + p[1]; }
std::int32_t i32(const unsigned char* p) {
  return static_cast<std::int32_t>((std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
                                   (std::uint32_t(p[2]) << 8) | p[3]);
}
Point point(const unsigned char* p) { return {i32(p), i32(p + 4)}; }
double real8(const unsigned char* p) {
  std::uint64_t mantissa = 0;
  for (unsigned i = 1; i < 8; ++i) mantissa = (mantissa << 8) | p[i];
  return (p[0] & 128 ? -1 : 1) * std::ldexp(double(mantissa), 4 * (int(p[0] & 127) - 64) - 56);
}

class Mapping {
 public:
  const unsigned char* data = nullptr;
  std::size_t size = 0;
  explicit Mapping(const std::filesystem::path& path) {
    const int descriptor = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) throw std::runtime_error("cannot open GDS: " + path.string());
    struct stat stat{};
    if (fstat(descriptor, &stat) || !S_ISREG(stat.st_mode) || stat.st_size <= 0 ||
        std::uintmax_t(stat.st_size) > std::numeric_limits<std::size_t>::max()) {
      close(descriptor);
      throw std::runtime_error("GDS must be a nonempty readable regular file: " + path.string());
    }
    size = std::size_t(stat.st_size);
    void* mapped = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, descriptor, 0);
    close(descriptor);
    if (mapped == MAP_FAILED) throw std::runtime_error("cannot mmap GDS: " + path.string());
    data = static_cast<const unsigned char*>(mapped);
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

constexpr unsigned Marker = 1, Keep = 2, Diagonal = 4, ZeroLength = 8;
struct Descriptor {
  std::uint64_t offset;
  std::uint32_t points;
  std::uint16_t layer;
  std::int8_t orientation;
  std::uint8_t flags;
};
static_assert(sizeof(Descriptor) == 16, "industrial GDS descriptor must remain compact");

struct Counts {
  std::uint64_t polygons = 0, vertices = 0, vertical = 0, cw = 0, ccw = 0, zero = 0, diagonal = 0;
};
struct Worker {
  std::vector<Counts> counts;
  std::vector<std::uint64_t> polygon_begin, vertex_begin, decoded_vertical;
  std::optional<Box> marker;
  std::uint64_t error_offset = std::numeric_limits<std::uint64_t>::max();
  const char* error = nullptr;
  std::uint64_t examined = 0, diagonal_examined = 0;
};
std::size_t cut(std::size_t total, unsigned part, unsigned parts) {
  return total / parts * part + total % parts * part / parts;
}

void scan(const Mapping& file, const std::array<int, 65536>& selected, int marker_layer,
          std::vector<Descriptor>& descriptors, GdsResult& result,
          const std::filesystem::path& path, std::vector<std::uint64_t>& selected_polygons,
          std::vector<std::uint64_t>& selected_vertices) {
  bool header = false, library = false, libname = false, units = false, cell = false,
       cell_name = false, cell_closed = false, ended = false;
  unsigned cells = 0;
  int element = -1, layer = -1, datatype = -1;
  bool have_xy = false;
  std::uint64_t xy_offset = 0;
  std::uint32_t coordinate_count = 0;
  std::uint64_t records = 0, boundaries = 0, ignored = 0, text = 0, markers = 0;
  for (std::size_t offset = 0; offset < file.size;) {
    auto fail = [&](const std::string& reason) {
      throw std::runtime_error(path.string() + ": " + reason + " at byte " +
                               std::to_string(offset));
    };
    if (file.size - offset < 4) fail("truncated GDS record header");
    const auto* p = file.data + offset;
    const unsigned length = u16(p), kind = p[2], dtype = p[3];
    if (length < 4 || (length & 1) || length > file.size - offset)
      fail("invalid GDS record length");
    const unsigned bytes = length - 4;
    const auto* payload = p + 4;
    auto fixed = [&](unsigned type, unsigned size) {
      if (dtype != type || bytes != size) fail("invalid GDS record type/size");
    };
    ++records;
    if (ended) fail("record after ENDLIB");
    if (!header && kind != 0) fail("missing HEADER");
    switch (kind) {
      case 0:
        fixed(2, 2);
        if (header) fail("duplicate HEADER");
        header = true;
        break;
      case 1:
        fixed(2, 24);
        if (library || cells) fail("duplicate or misplaced BGNLIB");
        library = true;
        break;
      case 2:
        if (!library || libname || cells || dtype != 6 || !bytes) fail("invalid LIBNAME");
        libname = true;
        break;
      case 3:
        fixed(5, 16);
        if (!library || units || cells) fail("duplicate or misplaced UNITS");
        result.dbu_nm = real8(payload + 8) * 1e9;
        if (!std::isfinite(real8(payload)) || real8(payload) <= 0 ||
            !std::isfinite(result.dbu_nm) || result.dbu_nm <= 0)
          fail("invalid UNITS");
        units = true;
        break;
      case 5:
        fixed(2, 24);
        if (!library || !libname || !units || cell || ++cells != 1)
          fail("expected exactly one flat cell");
        cell = true;
        break;
      case 6:
        if (!cell || cell_name || element >= 0 || dtype != 6 || !bytes) fail("invalid STRNAME");
        cell_name = true;
        break;
      case 7:
        fixed(0, 0);
        if (!cell || !cell_name || element >= 0) fail("invalid ENDSTR");
        cell = false;
        cell_closed = true;
        break;
      case 8:
      case 12:
        fixed(0, 0);
        if (!cell || !cell_name || element >= 0) fail("misplaced or nested element");
        element = int(kind);
        layer = datatype = -1;
        have_xy = false;
        coordinate_count = 0;
        break;
      case 9:
      case 10:
      case 11:
      case 21:
      case 45:
        fail("unsupported PATH/reference/NODE/BOX; GDS must be flat BOUNDARY geometry");
        break;
      case 13:
        fixed(2, 2);
        if (element < 0 || layer >= 0) fail("invalid or duplicate LAYER");
        layer = int(u16(payload));
        break;
      case 14:
        fixed(2, 2);
        if (element != 8 || datatype >= 0) fail("invalid or duplicate DATATYPE");
        datatype = int(u16(payload));
        break;
      case 16:
        if (element < 0 || have_xy || dtype != 3 || bytes % 8 ||
            (element == 8 ? bytes < 32 : bytes != 8))
          fail("invalid or duplicate XY");
        have_xy = true;
        xy_offset = offset + 4;
        coordinate_count = bytes / 8;
        break;
      case 17:
        fixed(0, 0);
        if (element < 0 || !have_xy || layer < 0 || (element == 8 && datatype < 0))
          fail("incomplete element");
        if (element == 8) {
          ++boundaries;
          const bool is_marker = layer == marker_layer && datatype == 0;
          const int slot = selected[std::size_t(layer)];
          if (is_marker || (slot >= 0 && datatype == 0)) {
            if (is_marker && ++markers != 1) fail("multiple selected marker polygons");
            descriptors.push_back({xy_offset, coordinate_count, std::uint16_t(layer), 0,
                                   std::uint8_t(is_marker ? Marker : 0)});
            if (!is_marker) {
              ++selected_polygons[slot];
              selected_vertices[slot] += coordinate_count;
            }
          } else
            ++ignored;
        } else
          ++text;
        element = -1;
        break;
      case 4:
        fixed(0, 0);
        if (!cell_closed || cell || element >= 0) fail("invalid ENDLIB");
        // GDS stream writers can pad the final physical block with zero bytes.
        // Accept only that padding, never another record or nonzero payload.
        if (std::any_of(file.data + offset + length, file.data + file.size,
                        [](unsigned char byte) { return byte != 0; }))
          fail("nonzero data after ENDLIB");
        result.counts["trailing_zero_padding_bytes"] = file.size - offset - length;
        ended = true;
        offset = file.size;
        continue;
      case 38:
        fixed(1, 2);
        if (element < 0) fail("ELFLAGS outside element");
        break;
      case 47:
        fixed(3, 4);
        if (element < 0) fail("PLEX outside element");
        break;
      case 43:
        fixed(2, 2);
        if (element < 0) fail("PROPATTR outside element");
        break;
      case 44:
        if (element < 0 || dtype != 6) fail("invalid PROPVALUE");
        break;
      case 22:
        fixed(2, 2);
        if (element != 12) fail("TEXTTYPE outside TEXT");
        break;
      case 23:
      case 26:
        fixed(1, 2);
        if (element != 12) fail("text metadata outside TEXT");
        break;
      case 27:
      case 28:
        fixed(5, 8);
        if (element != 12) fail("text transform outside TEXT");
        break;
      case 25:
        if (element != 12 || dtype != 6) fail("STRING outside TEXT");
        break;
      case 15:
        fixed(3, 4);
        if (element != 12) fail("WIDTH outside TEXT");
        break;
      case 33:
        fixed(2, 2);
        if (element != 12) fail("PATHTYPE outside TEXT");
        break;
      case 52:
        fixed(1, 2);
        if (!cell || element >= 0) fail("misplaced STRCLASS");
        break;
      default:
        fail("unsupported GDS record " + std::to_string(kind));
    }
    offset += length;
  }
  if (!header || !library || !units || !ended || cells != 1)
    throw std::runtime_error(path.string() + ": incomplete flat GDS");
  if (marker_layer >= 0 && markers != 1)
    throw std::runtime_error(path.string() + ": required marker is missing");
  result.counts["records"] = records;
  result.counts["boundary_polygons"] = boundaries;
  result.counts["ignored_other_layer_polygons"] = ignored;
  result.counts["text_elements"] = text;
  result.counts["marker_polygons"] = markers;
}

void filter_descriptor(const Mapping& file, Descriptor& descriptor, Worker& worker, int slot) {
  const auto* coordinates = file.data + descriptor.offset;
  const auto first = point(coordinates);
  auto fail = [&](const char* reason) {
    worker.error = reason;
    worker.error_offset = descriptor.offset;
  };
  if (first != point(coordinates + 8 * (descriptor.points - 1))) {
    fail("selected polygon is not explicitly closed");
    return;
  }
  __int128 area = 0;
  bool zero = false;
  std::uint64_t vertical = 0, examined = 0;
  Box box{first.x, first.y, first.x, first.y};
  auto previous = first;
  for (unsigned j = 1; j < descriptor.points; ++j) {
    const auto current = point(coordinates + 8 * j);
    ++examined;
    if (previous.x != current.x && previous.y != current.y) {
      worker.examined += examined;
      if (descriptor.flags & Marker) {
        fail("marker contains a diagonal edge");
        return;
      }
      descriptor.flags |= Diagonal;
      ++worker.counts[slot].diagonal;
      worker.diagonal_examined += examined;
      return;  // Never inspect or allocate the remainder of a rejected contour.
    }
    zero |= previous == current;
    vertical += previous.x == current.x && previous.y != current.y;
    area += __int128(previous.x) * current.y - __int128(current.x) * previous.y;
    if (descriptor.flags & Marker) {
      box.xmin = std::min(box.xmin, current.x);
      box.xmax = std::max(box.xmax, current.x);
      box.ymin = std::min(box.ymin, current.y);
      box.ymax = std::max(box.ymax, current.y);
    }
    previous = current;
  }
  worker.examined += examined;
  if (!area) {
    fail("selected polygon has zero signed area");
    return;
  }
  if (descriptor.flags & Marker) {
    if (box.xmin == box.xmax || box.ymin == box.ymax) {
      fail("marker must have positive size");
      return;
    }
    const auto expected = 2 * (__int128(box.xmax) - box.xmin) * (__int128(box.ymax) - box.ymin);
    if ((area < 0 ? -area : area) != expected) {
      fail("marker does not fill its rectangular bounding box");
      return;
    }
    previous = first;
    for (unsigned j = 1; j < descriptor.points; ++j) {
      const auto current = point(coordinates + 8 * j);
      const bool on_vertical =
          previous.x == current.x && (previous.x == box.xmin || previous.x == box.xmax);
      const bool on_horizontal =
          previous.y == current.y && (previous.y == box.ymin || previous.y == box.ymax);
      if (!on_vertical && !on_horizontal) {
        fail("marker has an interior segment");
        return;
      }
      previous = current;
    }
    worker.marker = box;
    return;
  }
  descriptor.flags |= Keep | (zero ? ZeroLength : 0);
  descriptor.orientation = area > 0 ? 1 : -1;
  auto& count = worker.counts[slot];
  ++count.polygons;
  count.vertices += descriptor.points;
  count.vertical += vertical;
  count.cw += area < 0;
  count.ccw += area > 0;
  count.zero += zero;
}

void check_workers(const std::vector<Worker>& workers, const std::filesystem::path& path) {
  const Worker* first = nullptr;
  for (const auto& worker : workers)
    if (worker.error && (!first || worker.error_offset < first->error_offset)) first = &worker;
  if (first)
    throw std::runtime_error(path.string() + ": " + first->error + " at XY byte " +
                             std::to_string(first->error_offset));
}

struct Allocation {
  unsigned char* data;
  std::size_t bytes;
};
template <class T>
void reserve(std::vector<T>& output, std::uint64_t count, std::vector<Allocation>& allocations) {
  if (count > output.max_size()) throw std::length_error("selected GDS exceeds vector capacity");
  output.reserve(std::size_t(count));
  if (count)
    allocations.push_back(
        {reinterpret_cast<unsigned char*>(output.data()), std::size_t(count) * sizeof(T)});
}

void touch(const std::vector<Allocation>& allocations, unsigned threads) {
  const auto page_size = std::size_t(std::max<long>(1, sysconf(_SC_PAGESIZE)));
// Write allocator-owned byte storage before ordinary vector resize constructs
// its elements. Each worker owns a disjoint byte range; no polygon shards or
// second geometry buffers are created. These writes only fault pages in.
#pragma omp parallel for num_threads(threads) schedule(static)
  for (int worker = 0; worker < int(threads); ++worker)
    for (const auto& allocation : allocations) {
      const auto begin = cut(allocation.bytes, worker, threads),
                 end = cut(allocation.bytes, worker + 1, threads);
      if (begin == end) continue;
      volatile unsigned char* bytes = allocation.data;
      bytes[begin] = 0;
      auto position =
          begin +
          (page_size - (reinterpret_cast<std::uintptr_t>(allocation.data) + begin) % page_size) %
              page_size;
      for (; position < end; position += page_size) bytes[position] = 0;
    }
}
}  // namespace

GdsResult read_gds_multi(const std::filesystem::path& path,
                         const std::vector<int>& requested_layers, int threads, int marker_layer) {
  const auto started = Clock::now();
  if (threads < 1 || threads > 256 || requested_layers.empty() || marker_layer < -1 ||
      marker_layer > 65535)
    throw std::invalid_argument("invalid industrial GDS layer/thread selection");
  auto layers = requested_layers;
  std::sort(layers.begin(), layers.end());
  if (std::adjacent_find(layers.begin(), layers.end()) != layers.end())
    throw std::invalid_argument("duplicate selected GDS layers");
  std::array<int, 65536> selected;
  selected.fill(-1);
  for (std::size_t i = 0; i < layers.size(); ++i) {
    if (layers[i] < 0 || layers[i] > 65535 || layers[i] == marker_layer)
      throw std::invalid_argument("invalid or overlapping GDS geometry/marker layers");
    selected[std::size_t(layers[i])] = int(i);
  }
  GdsResult result;
  auto phase = Clock::now();
  Mapping file(path);
  result.milliseconds["map_ms"] = ms(phase);
  result.counts["file_bytes"] = file.size;
  result.counts["file_mappings"] = 1;
  result.counts["record_scans"] = 1;
  result.counts["threads_requested"] = threads;
  phase = Clock::now();
  std::vector<Descriptor> descriptors;
  // A structurally valid BOUNDARY with four XY points occupies at least 56
  // bytes. Such a closed triangle may be discarded as diagonal; assuming 64
  // bytes would not bound its descriptor count. reserve does not touch unused
  // capacity, and this bound prevents descriptor-vector growth/copy peaks.
  const auto descriptor_capacity = file.size / 56 + 1;
  if (descriptor_capacity > descriptors.max_size())
    throw std::length_error("GDS descriptor capacity overflow");
  descriptors.reserve(descriptor_capacity);
  std::vector<std::uint64_t> selected_polygons(layers.size()), selected_vertices(layers.size());
  scan(file, selected, marker_layer, descriptors, result, path, selected_polygons,
       selected_vertices);
  result.milliseconds["record_scan_ms"] = ms(phase);
  result.counts["descriptor_count"] = descriptors.size();
  result.counts["descriptor_bytes"] = descriptors.size() * sizeof(Descriptor);
  result.counts["descriptor_capacity_bytes"] = descriptors.capacity() * sizeof(Descriptor);
  phase = Clock::now();
  std::vector<Worker> workers(threads);
  for (auto& worker : workers) {
    worker.counts.resize(layers.size());
    worker.polygon_begin.resize(layers.size());
    worker.vertex_begin.resize(layers.size());
    worker.decoded_vertical.resize(layers.size());
  }
  int actual_threads = 1;
#pragma omp parallel for num_threads(threads) schedule(static)
  for (int number = 0; number < threads; ++number) {
    if (!number) actual_threads = omp_get_num_threads();
    auto& worker = workers[number];
    for (auto i = cut(descriptors.size(), number, threads),
              end = cut(descriptors.size(), number + 1, threads);
         i < end; ++i) {
      auto& descriptor = descriptors[i];
      filter_descriptor(file, descriptor, worker, selected[descriptor.layer]);
      if (worker.error) break;
    }
  }
  check_workers(workers, path);
  for (const auto& worker : workers) {
    if (worker.marker) result.marker = worker.marker;
    result.counts["filter_segments_examined"] += worker.examined;
    result.counts["diagonal_filter_segments_examined"] += worker.diagonal_examined;
  }
  result.counts["threads_used"] = actual_threads;
  result.milliseconds["filter_count_ms"] = ms(phase);
  phase = Clock::now();
  std::vector<Counts> totals(layers.size());
  std::vector<Polygons*> output;
  std::vector<Allocation> allocations;
  for (std::size_t slot = 0; slot < layers.size(); ++slot) {
    auto& total = totals[slot];
    for (auto& worker : workers) {
      worker.polygon_begin[slot] = total.polygons;
      worker.vertex_begin[slot] = total.vertices;
      const auto& count = worker.counts[slot];
      total.polygons += count.polygons;
      total.vertices += count.vertices;
      total.vertical += count.vertical;
      total.cw += count.cw;
      total.ccw += count.ccw;
      total.zero += count.zero;
      total.diagonal += count.diagonal;
    }
    auto& polygons = result.layers[layers[slot]];
    reserve(polygons.vertices, total.vertices, allocations);
    reserve(polygons.offsets, total.polygons + 1, allocations);
    reserve(polygons.boxes, total.polygons, allocations);
    reserve(polygons.orientation, total.polygons, allocations);
    output.push_back(&polygons);
  }
  const auto touching = Clock::now();
  touch(allocations, unsigned(threads));
  result.milliseconds["first_touch_ms"] = ms(touching);
  for (std::size_t slot = 0; slot < layers.size(); ++slot) {
    auto& polygons = *output[slot];
    const auto& total = totals[slot];
    polygons.vertices.resize(total.vertices);
    polygons.offsets.resize(total.polygons + 1);
    polygons.boxes.resize(total.polygons);
    polygons.orientation.resize(total.polygons);
    polygons.offsets.back() = total.vertices;
    auto& counts = result.layer_counts[layers[slot]];
    counts["selected_polygons"] = selected_polygons[slot];
    counts["selected_vertices"] = selected_vertices[slot];
    counts["retained_polygons"] = total.polygons;
    counts["retained_vertices"] = total.vertices;
    counts["discarded_diagonal_polygons"] = total.diagonal;
    counts["raw_vertical_edges"] = total.vertical;
    counts["clockwise_polygons"] = total.cw;
    counts["counterclockwise_polygons"] = total.ccw;
    counts["polygons_with_zero_length_edges"] = total.zero;
    for (const auto& count : counts) result.counts[count.first] += count.second;
    result.counts["output_storage_bytes"] +=
        polygons.vertices.size() * sizeof(Point) + polygons.offsets.size() * sizeof(std::uint64_t) +
        polygons.boxes.size() * sizeof(Box) + polygons.orientation.size();
  }
  result.milliseconds["allocation_ms"] = ms(phase);
  phase = Clock::now();
  const auto marker = result.marker;
#pragma omp parallel for num_threads(threads) schedule(static)
  for (int number = 0; number < threads; ++number) {
    auto& worker = workers[number];
    auto polygon_position = worker.polygon_begin, vertex_position = worker.vertex_begin;
    for (auto i = cut(descriptors.size(), number, threads),
              end = cut(descriptors.size(), number + 1, threads);
         i < end; ++i) {
      const auto& descriptor = descriptors[i];
      if (!(descriptor.flags & Keep)) continue;
      const auto slot = std::size_t(selected[descriptor.layer]);
      auto& polygons = *output[slot];
      const auto index = polygon_position[slot]++, begin = vertex_position[slot];
      const auto* coordinates = file.data + descriptor.offset;
      auto previous = point(coordinates);
      Box box{previous.x, previous.y, previous.x, previous.y};
      std::uint64_t vertical = 0;
      for (unsigned j = 0; j < descriptor.points; ++j) {
        const auto current = point(coordinates + 8 * j);
        polygons.vertices[begin + j] = current;
        box.xmin = std::min(box.xmin, current.x);
        box.xmax = std::max(box.xmax, current.x);
        box.ymin = std::min(box.ymin, current.y);
        box.ymax = std::max(box.ymax, current.y);
        vertical += j && previous.x == current.x && previous.y != current.y;
        previous = current;
      }
      if (marker && (box.xmin < marker->xmin || box.xmax > marker->xmax ||
                     box.ymin < marker->ymin || box.ymax > marker->ymax)) {
        worker.error = "selected template material lies outside its marker";
        worker.error_offset = descriptor.offset;
        break;
      }
      polygons.offsets[index] = begin;
      polygons.boxes[index] = box;
      polygons.orientation[index] = descriptor.orientation;
      vertex_position[slot] += descriptor.points;
      worker.decoded_vertical[slot] += vertical;
    }
  }
  check_workers(workers, path);
  for (std::size_t slot = 0; slot < layers.size(); ++slot) {
    std::uint64_t decoded = 0;
    for (const auto& worker : workers) decoded += worker.decoded_vertical[slot];
    if (decoded != totals[slot].vertical)
      throw std::logic_error("GDS vertical-edge count changed between filtering and decode");
  }
  result.milliseconds["decode_ms"] = ms(phase);
  phase = Clock::now();
  std::vector<Descriptor>().swap(descriptors);
  file.release();
  result.milliseconds["unmap_ms"] = ms(phase);
  result.milliseconds["wall_ms"] = ms(started);
  return result;
}
}  // namespace unimatcher::io
