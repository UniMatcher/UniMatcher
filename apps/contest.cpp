#include <unimatcher/contest.hpp>
#include <charconv>
#include <iostream>
#include <string_view>
#include <vector>

namespace {
void usage(std::ostream& out) {
    out << "Usage:\n"
        << "  unimatcher-contest 2023 [--device N] [--identity] LAYOUT OUTPUT_DIR PATTERN...\n"
        << "  unimatcher-contest 2024 [--device N] LAYOUT OUTPUT_FILE PATTERN_LIBRARY\n";
}
}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        usage(std::cout);
        return 0;
    }
    if (argc < 5) {
        usage(std::cerr);
        return 2;
    }
    const std::string_view year = argv[1];
    bool all_orientations = true;
    int device = 0;
    int i = 2;
    while (i < argc) {
        const std::string_view option = argv[i];
        if (option == "--identity" && year == "2023") {
            all_orientations = false;
            ++i;
        } else if (option == "--device" && i + 1 < argc) {
            const std::string_view argument = argv[i + 1];
            const auto end = argument.data() + argument.size();
            const auto number = std::from_chars(argument.data(), end, device);
            if (number.ec != std::errc{} || number.ptr != end || device < 0) {
                std::cerr << "Expected a nonnegative GPU device index.\n";
                return 2;
            }
            i += 2;
        } else if (option.substr(0, 2) == "--") {
            usage(std::cerr);
            return 2;
        } else {
            break;
        }
    }
    if (year == "2023" && argc - i >= 3) {
        const std::vector<const char*> patterns(argv + i + 2, argv + argc);
        return unimatcher::run_contest_2023(argv[i], patterns.data(), patterns.size(),
                                            argv[i + 1], all_orientations, device);
    }
    if (year == "2024" && argc - i == 3) {
        return unimatcher::run_contest_2024(argv[i], argv[i + 2], argv[i + 1], device);
    }
    usage(std::cerr);
    return 2;
}
