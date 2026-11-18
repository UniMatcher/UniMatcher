#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace unimatcher {

// A vertical segment at x over [lo, hi). Sweeping toward +x enters material
// for sign = +1 and leaves it for sign = -1. Coordinates are integer DBU.
struct alignas(16) Edge {
    std::int32_t x, lo, hi, sign;
};
struct Box {
    std::int64_t xmin, ymin, xmax, ymax;
};
struct Point {
    std::int32_t x, y;
};
struct FuzzyContour {
    // Contours list vertices in cyclic order, without a duplicate closing vertex.
    std::vector<Point> nominal;
    // Corresponding vertices at the extreme displacement. Empty for fixed geometry.
    std::vector<Point> alternate;
};

// Compiled templates are immutable and can be shared across indexes.
class Pattern;
class FuzzyPattern;
// Union the template, then compile R0 or all eight orientations. The marker
// encloses all template geometry; its coordinates and dimensions fit int32.
Pattern prepare_pattern(const std::vector<Edge>& raw_edges, Box marker,
                        bool all_orientations = true);
// Compile disjoint rectilinear contours with uniquely identifiable boundaries
// and topology-preserving displacements. A variant supports up to 16 movable
// support lines and 64 interior edges, including one complete fixed anchor.
FuzzyPattern prepare_fuzzy_pattern(const std::vector<FuzzyContour>& contours,
                                   Box marker, bool all_orientations = true);

class Pattern {
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    explicit Pattern(std::shared_ptr<const Impl>);
    friend class Index;
    friend Pattern prepare_pattern(const std::vector<Edge>&, Box, bool);
};

class FuzzyPattern {
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    explicit FuzzyPattern(std::shared_ptr<const Impl>);
    friend class Index;
    friend FuzzyPattern prepare_fuzzy_pattern(const std::vector<FuzzyContour>&, Box, bool);
};

struct WindowEdges {
    // Edges use coordinates relative to each window's lower-left corner.
    // Window i owns edges[offsets[i] .. offsets[i + 1]).
    std::vector<std::uint64_t> offsets;
    std::vector<Edge> edges;
};

// A single index owns its device allocations. Serialize queries on one index.
class Index {
public:
    // Normalize raw signed edges on the chosen CUDA device, then retain
    // its refitted LBVH for subsequent queries. Each owner_xmax[i] must
    // be the rightmost x of the entire polygon that contributed raw[i].
    Index(const std::vector<Edge>& raw, const std::vector<std::int32_t>& owner_xmax,
          int device = 0);
    // Build directly from maximal Boolean boundary segments (already unioned).
    static Index from_canonical(const std::vector<Edge>& canonical, int device = 0);
    ~Index();
    Index(const Index&) = delete;
    Index& operator=(const Index&) = delete;

    std::vector<Edge> edges() const;

    // One group per orientation: R0, R90, R180, R270, then mirror about the
    // y axis followed by those rotations. Single-orientation queries use R0.
    // Searching requires a complete fixed vertical anchor inside each marker.
    std::vector<std::vector<Box>> match(const Pattern&);
    std::vector<std::vector<Box>> match(const FuzzyPattern&);

    // Verify fixed positions against one orientation, including empty layers
    // and templates without anchors. Each output flag corresponds to one input.
    std::vector<std::uint8_t> verify_windows(const Pattern&, std::size_t orientation,
                                             const std::vector<Box>& windows);
    WindowEdges xor_windows(const Pattern&, std::size_t orientation,
                            const std::vector<Box>& windows);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit Index(std::unique_ptr<Impl>);
};

}  // namespace unimatcher
