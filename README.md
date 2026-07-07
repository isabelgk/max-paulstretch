# max-paulstretch

Max/MSP externals for the Paulstretch extreme time-stretching algorithm.

## Installing

Download the latest build of the package from the GitHub
[Releases](https://github.com/isabelgk/max-paulstretch/releases) page. Unzip
`package.zip` into your Max Packages directory.

## Building

### Directly with CMake

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
```
The Max package you can add to `~/Documents/Max 9/Packages/` is produced under
`build-*/package/`.

### With `mise`

`mise` automatically manages dev tools and environment setup. It will install
`just` for this project which is used to run common tasks.

Install [mise](https://mise.jdx.dev/).

```
mise install
just build-release-mac # or just build-release-win
```

To list available commands, run `just` on it's own.
