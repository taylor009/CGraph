set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)

# Clang cannot consume GCC's compiler-specific LTO objects from static archives.
set(VCPKG_CMAKE_CONFIGURE_OPTIONS -DIGRAPH_ENABLE_LTO=OFF)
