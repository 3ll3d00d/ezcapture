# Agent instructions

## Keep MSBuild and CMake build configs in sync

This project builds with two parallel systems: the MSBuild `.vcxproj`/`.sln` files
(used by CI release builds, `.github/workflows/msbuild.yml`) and `CMakeLists.txt`
+ `CMakePresets.json` (used for CLion/cross-editor development). Both must invoke
MSVC with equivalent settings so they produce equivalent binaries — MinGW/GCC is
not a supported alternative toolchain for this codebase (it relies on ATL,
MIDL-generated COM interop, and other MSVC-specific extensions).

**Whenever you change a compiler/linker setting in one build system, make the
matching change in the other.** This includes but is not limited to: optimization
level, `/arch:` flags (e.g. AVX2), warning level, runtime library
(`/MD`/`/MDd` etc.), preprocessor defines, and C++ standard version.

Before committing a change to any `.vcxproj`, `.props`, `CMakeLists.txt`, or
`CMakePresets.json`, check whether the corresponding setting exists in the other
build system and update it too.
