# TreeMake

A lightweight C++ desktop app that parses `CMakePresets.json` files and displays a navigable tree of all presets with fully **resolved** CMake flags (flattening the `inherits` chain).

THIS IS SUPER ALPHA STAGE, PLEASE DON'T RELY ON IT WORKING PERFECTLY!

![Stack: C++17 · Dear ImGui · GLFW/OpenGL3 · nlohmann/json](https://img.shields.io/badge/stack-C%2B%2B17%20%7C%20ImGui%20%7C%20GLFW%20%7C%20nlohmann%2Fjson-blue)

## Features

- Parses `configurePresets`, `buildPresets`, `testPresets`, `packagePresets`, `workflowPresets`
- Automatically merges `CMakeUserPresets.json` if found next to the file
- Resolves the full `inherits` chain (child overrides parent, rightmost parent = lowest priority)
- Shows all resolved cache variables with aligned formatting
- Generates an equivalent `cmake` command line you can copy-paste
- Toggle to show/hide hidden presets
- Accepts file path via command line or the GUI text field

## Requirements

- CMake ≥ 3.20
- A C++17 compiler (GCC 9+, Clang 10+, MSVC 2019+)
- OpenGL 3.3 capable GPU/driver
- **Linux**: `libgl-dev`, `libx11-dev`, `libxrandr-dev`, `libxinerama-dev`, `libxcursor-dev`, `libxi-dev` (or equivalent Wayland packages)
- **macOS / Windows**: no extra deps

All library dependencies (Dear ImGui, GLFW, nlohmann/json) are fetched automatically via CMake `FetchContent`.

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## Run

```bash
# Pass a preset file on the command line…
./build/preset_viewer /path/to/CMakePresets.json

# …or type/paste the path in the GUI text field and hit Enter / Load.
```

## How inheritance resolution works

Given:

```json
{
  "configurePresets": [
    { "name": "base",  "hidden": true, "cacheVariables": { "CMAKE_BUILD_TYPE": "Release" } },
    { "name": "linux", "inherits": "base", "generator": "Ninja",
      "cacheVariables": { "CMAKE_CXX_FLAGS": "-Wall" } }
  ]
}
```

Selecting **linux** will show:

```
generator      : Ninja
-DCMAKE_BUILD_TYPE = Release     ← inherited from base
-DCMAKE_CXX_FLAGS  = -Wall      ← defined in linux
```
