#pragma once
#include <cstddef>

namespace unimatcher {

// Match all supplied 2023 TXT patterns against one layout. Bracketed alternate
// contours select constrained fuzzy matching. Output paths must be new. Each
// pattern_i/marker.txt contains one rectangle per line. Returns zero on success
// and prints input or device errors to stderr.
int run_contest_2023(const char* layout, const char* const* patterns,
                     std::size_t pattern_count, const char* output_directory,
                     bool all_orientations = true, int device = 0);

// The 2024 pattern library can contain multiple templates. Writes mismatching
// layers and XOR polygons to output_file, plus a .json metadata sidecar.
// Returns zero on success and prints input or device errors to stderr.
int run_contest_2024(const char* layout, const char* pattern_library,
                     const char* output_file, int device = 0);

}  // namespace unimatcher
