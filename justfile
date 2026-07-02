alias fmt := format

default:
    just -l

configure-debug-mac:
    cmake -G Ninja -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug

configure-release-mac:
    cmake -G Ninja -S . -B build-release -DCMAKE_BUILD_TYPE=Release

configure-debug-win:
    cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug

configure-release-win:
    cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release

build-debug:
    cmake --build build-debug

build-release:
    cmake --build build-release

format:
    clang-format -i source/**/*.h
    clang-format -i source/**/*.cpp
