find_package(OpenMP REQUIRED COMPONENTS CXX)

add_library(unimatcher_io STATIC
    txt/reader.cpp
    gds/reader.cpp
    gds/parallel_reader.cpp
    signed_edges.cpp)
target_link_libraries(unimatcher_io PRIVATE OpenMP::OpenMP_CXX)
add_library(UniMatcher::io ALIAS unimatcher_io)
target_compile_features(unimatcher_io PUBLIC cxx_std_17)
target_include_directories(unimatcher_io PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>"
    "$<INSTALL_INTERFACE:include>")
