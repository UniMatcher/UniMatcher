#include <unimatcher/core.hpp>

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
using namespace unimatcher;

void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
void check_rectangle(std::vector<Edge> edges, int left, int bottom, int right, int top) {
    std::sort(edges.begin(), edges.end(), [](auto a, auto b) { return a.x < b.x; });
    require(edges.size() == 2, "rectangle should have two canonical edges");
    require(edges[0].x == left && edges[0].sign == 1 && edges[1].x == right &&
            edges[1].sign == -1, "rectangle x or winding differs");
    for (auto e : edges) require(e.lo == bottom && e.hi == top, "rectangle height differs");
}
void check_marker(const std::vector<Box>& group) {
    require(group.size() == 1, "expected one match");
    const auto b = group.front();
    require(b.xmin == 10 && b.ymin == 20 && b.xmax == 20 && b.ymax == 30, "unexpected marker");
}
}

int main() {
    try {
        // Two overlapping rectangles describe a single material region.
        Index index({{12,22,25,+1},{16,22,25,-1},{14,22,25,+1},{18,22,25,-1}},
                    {16,16,18,18});
        const auto canonical = index.edges();
        check_rectangle(canonical, 12, 22, 18, 25);

        const Box marker{0,0,10,10};
        const auto pattern = prepare_pattern({{2,2,5,+1},{8,2,5,-1}}, marker);
        const auto matches = index.match(pattern);
        require(matches.size() == 8, "missing orientations");
        check_marker(matches[0]);
        auto direct = Index::from_canonical(canonical);
        check_marker(direct.match(pattern)[0]);

        const std::vector<Box> positions{{10,20,20,30},{100,100,110,110}};
        require(index.verify_windows(pattern, 0, positions) == std::vector<std::uint8_t>{1,0},
                "fixed-position verification differs");
        const auto empty_layer = prepare_pattern({}, marker, false);
        require(index.verify_windows(empty_layer, 0, positions) == std::vector<std::uint8_t>{0,1},
                "empty-layer verification differs");
        const auto delta = index.xor_windows(empty_layer, 0, positions);
        require(delta.offsets == std::vector<std::uint64_t>{0,2,2}, "wrong XOR offsets");
        check_rectangle(delta.edges, 2, 2, 8, 5);

        const FuzzyContour flexible{{{2,2},{6,2},{6,5},{2,5}},
                                    {{2,2},{9,2},{9,5},{2,5}}};
        check_marker(index.match(prepare_fuzzy_pattern({flexible}, marker, false))[0]);
        std::cout << "Union, exact and fuzzy matching, fixed windows, and XOR passed.\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
