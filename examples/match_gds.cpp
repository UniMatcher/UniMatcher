#include <unimatcher/core.hpp>
#include <unimatcher/io/gds.hpp>
#include <unimatcher/io/signed_edges.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>

// Search one exact orientation on geometry layer 1/0 with a marker on 2/0.
// Output columns are xmin ymin xmax ymax, measured in layout database units.
int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: unimatcher-gds LAYOUT.gds PATTERN.gds\n";
        return 2;
    }
    try {
        namespace io = unimatcher::io;
        constexpr int layer = 1, marker_layer = 2;
        const auto layout = io::read_gds_multi(argv[1], {layer});
        const auto pattern = io::read_gds_multi(argv[2], {layer}, 1, marker_layer);
        // GDSII REAL8 encodings for one physical grid can differ by a rounding bit.
        const double tolerance = 4 * std::numeric_limits<double>::epsilon() *
                                 std::max(layout.dbu_nm, pattern.dbu_nm);
        if (std::abs(layout.dbu_nm - pattern.dbu_nm) > tolerance)
            throw std::invalid_argument("Layout and template must have the same DBU.");
        if (layout.counts.at("discarded_diagonal_polygons") ||
            pattern.counts.at("discarded_diagonal_polygons"))
            throw std::invalid_argument("This example requires Manhattan geometry.");

        const auto raw = io::extract_signed_edges(layout.layers.at(layer));
        unimatcher::Index index(raw.edges, raw.polygon_xmax);
        const auto template_edges = io::extract_signed_edges(pattern.layers.at(layer), 1, false);
        const auto query = unimatcher::prepare_pattern(template_edges.edges, io::to_box(*pattern.marker), false);
        const auto matches = index.match(query);
        for (const auto& box : matches.at(0))
            std::cout << box.xmin << ' ' << box.ymin << ' ' << box.xmax << ' ' << box.ymax << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
