#include <unimatcher/io/gds.hpp>
#include <unimatcher/io/signed_edges.hpp>

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace io = unimatcher::io;

namespace {
void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

void check_edges(const std::vector<unimatcher::Edge>& actual,
                 const std::vector<unimatcher::Edge>& expected) {
    require(actual.size() == expected.size(), "unexpected edge count");
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const auto a = actual[i], e = expected[i];
        require(a.x == e.x && a.lo == e.lo && a.hi == e.hi && a.sign == e.sign,
                "signed edges differ in value or input order");
    }
}
}  // namespace

int main() try {
    const auto fixture = std::filesystem::path(__FILE__).parent_path() /
                         "fixtures" / "parallel_layers.gds";
    const auto gds = io::read_gds_multi(fixture, {3, 1, 42}, 2, 9);
    require(std::abs(gds.dbu_nm - 2) < 1e-12 && gds.marker &&
            gds.marker->xmin == -8 && gds.marker->ymin == -6 &&
            gds.marker->xmax == 26 && gds.marker->ymax == 14, "marker or DBU differs");
    require(gds.counts.at("file_mappings") == 1 && gds.counts.at("record_scans") == 1 &&
            gds.counts.at("trailing_zero_padding_bytes") == 23, "GDS traversal differs");

    // Two overlapping material contours on layer 3 have opposite winding and
    // are interleaved with another layer in the file. A datatype-1 boundary
    // located outside the marker must be ignored.
    const auto& layer = gds.layers.at(3);
    require(layer.offsets == std::vector<std::uint64_t>{0, 7, 12}, "polygon offsets differ");
    require(layer.orientation == std::vector<std::int8_t>{-1, 1}, "winding differs");
    require(layer.boxes.size() == 2 && layer.boxes[0].xmin == -4 &&
            layer.boxes[0].xmax == 7 && layer.boxes[1].xmax == 5, "polygon bounds differ");
    const auto edges = io::extract_signed_edges(layer, 2);
    const std::vector<unimatcher::Edge> expected{
        {-4,-2,5,1}, {7,0,5,-1}, {2,-2,0,-1}, {5,4,8,-1}, {0,4,8,1}};
    check_edges(edges.edges, expected);
    require(edges.polygon_xmax == std::vector<std::int32_t>{7,7,7,5,5},
            "polygon influence bounds differ");
    const auto no_owners = io::extract_signed_edges(layer, 1, false);
    check_edges(no_owners.edges, expected);
    require(no_owners.polygon_xmax.empty(), "owner-free extraction produced influence bounds");

    // The triangle on layer 1 is discarded as a whole. The duplicate vertex
    // in the retained rectangle survives parsing and produces no zero-length edge.
    const auto& first = gds.layers.at(1);
    require(gds.layer_counts.at(1).at("discarded_diagonal_polygons") == 1 &&
            gds.layer_counts.at(1).at("polygons_with_zero_length_edges") == 1 &&
            first.offsets == std::vector<std::uint64_t>{0, 6}, "polygon filtering differs");
    check_edges(io::extract_signed_edges(first, 2).edges, {{12,0,3,-1}, {8,0,3,1}});
    const auto& empty = gds.layers.at(42);
    require(empty.offsets == std::vector<std::uint64_t>{0} &&
            io::extract_signed_edges(empty, 2).edges.empty(), "empty selected layer differs");

    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
