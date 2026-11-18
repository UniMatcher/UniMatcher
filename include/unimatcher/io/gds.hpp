#pragma once

#include <unimatcher/io/geometry.hpp>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace unimatcher::io {

struct GdsStats {
  double map_ms = 0, scan_ms = 0, allocation_ms = 0, decode_ms = 0;
  double unmap_ms = 0, wall_ms = 0;
  std::uint64_t file_bytes = 0, records = 0, boundary_polygons = 0;
  std::uint64_t selected_polygons = 0, selected_vertices = 0;
  std::uint64_t retained_polygons = 0, retained_vertices = 0;
  std::uint64_t discarded_diagonal_polygons = 0, ignored_other_layer_polygons = 0;
  std::uint64_t text_elements = 0, raw_vertical_edges = 0;
  std::uint64_t clockwise_polygons = 0, counterclockwise_polygons = 0;
  std::uint64_t polygons_with_zero_length_edges = 0;
};
struct GdsOptions {
  int layer = 1, datatype = 0;
  int marker_layer = -1, marker_datatype = 0;
  bool require_marker = false;
};
struct GdsData {
  Polygons polygons;
  double dbu_nm = 0;
  std::optional<Box> marker;
  GdsStats stats;
};

// One flat cell, selected BOUNDARY layer/datatype; TEXT is ignored. PATH and
// references are rejected. Whole polygons with diagonal edges are discarded.
// Retained polygons must be explicitly closed and have nonzero signed area;
// duplicate consecutive points are retained. Empty selected geometry is valid.
// A requested marker is one positive-size axis-aligned rectangle (zero-length
// and collinear subdivision points are accepted only along its perimeter).
// All coordinates remain integer source DBU; callers compare layout/pattern DBU.
GdsData read_gds(const std::filesystem::path& path, const GdsOptions& options = {});

// Layers retain source polygon order and explicitly closed integer coordinates.
// Every requested layer has an entry, including layers with no retained geometry.
struct GdsResult {
  std::map<int, Polygons> layers;
  double dbu_nm = 0;
  std::optional<Box> marker;
  std::map<std::string, double> milliseconds;
  std::map<std::string, std::uint64_t> counts;
  std::map<int, std::map<std::string, std::uint64_t>> layer_counts;
};

// The paper's industrial reader maps one flat cell and scans its record headers
// once for all requested BOUNDARY layers, then filters and decodes polygons in
// parallel. Geometry and marker use datatype 0. A selected polygon must be
// explicitly closed. A diagonal segment discards its entire polygon.
// Supply distinct layer numbers and 1..256 OpenMP threads. A nonnegative
// marker_layer requires one rectangular marker on a separate layer containing
// all selected material. Coordinates stay in DBU; dbu_nm is the scale in nm.
GdsResult read_gds_multi(const std::filesystem::path& path, const std::vector<int>& layers,
                         int threads = 64, int marker_layer = -1);

}  // namespace unimatcher::io
