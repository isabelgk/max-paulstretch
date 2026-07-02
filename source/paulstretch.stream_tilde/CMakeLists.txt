set_property(DIRECTORY PROPERTY max::external YES)
project(paulstretch.stream_tilde)

target_sources(paulstretch.stream_tilde
    PRIVATE
        paulstretch.stream_tilde.cpp)

target_include_directories(paulstretch.stream_tilde
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../common)

target_link_libraries(paulstretch.stream_tilde
    PRIVATE
        paulstretch_core)

set_target_properties(paulstretch.stream_tilde PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
