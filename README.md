# max-paulstretch

Max/MSP externals for the Paulstretch extreme time-stretching algorithm.

## Installing

Packages built for macOS and Windows are available on the GitHub
[Releases](https://github.com/isabelgk/max-paulstretch/releases) page as
`paulstretch.maxpack`. (Older versions are `package.zip`.)

Download this file and choose one of the following options for install:

1. Drag and drop the `maxpack` file onto the Max **console** (not the patcher
   window).
2. Place the `maxpack` file in your Max packages directory (for example,
   `~/Documents/Max 9/Packages/`) and start Max.
3. Rename the file to have a `zip` extension (e.g. `paulstretch.zip`) and unzip
   to your Max packages directory.

If you are not sure what to choose, try option 1 first.

Options 1 and 2 automatically handle xattr quarantine removal on every external
so you do not need to click accept on the "External cannot be loaded due to
macOS quarantine, Max can attempt to remove quarantine attributes" every time a
new external is loaded.

## Building

### Directly with CMake

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
```
The Max package you can add to `~/Documents/Max 9/Packages/` is produced under
`build-*/package/`.

### With `mise`

`mise` automatically manages dev tools and environment setup, and provides
tasks for running common commands.

Install [mise](https://mise.jdx.dev/).

```
mise install
mise run configure-release-mac # or mise run configure-release-win
mise run build-release
```

To list available tasks, run `mise tasks`.
