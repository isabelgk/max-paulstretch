set_property(DIRECTORY PROPERTY max::external YES)
project(paulstretch_tilde) # output bundle: paulstretch~.mxo (_tilde->~)

target_sources(paulstretch_tilde
    PRIVATE
        paulstretch_tilde.cpp)

target_include_directories(paulstretch_tilde
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../common)

target_link_libraries(paulstretch_tilde
    PRIVATE
        paulstretch_core)

# Override the SDK's default C++17 — the library is C++20 (see paulstretch.stream_tilde).
set_target_properties(paulstretch_tilde PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
