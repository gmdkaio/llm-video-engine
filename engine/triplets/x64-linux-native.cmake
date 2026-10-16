# Local Linux builds tuned for the machine they run on: clang, -march=native, static libraries,
# release only. Not portable: the Claude sandbox bundle uses x64-linux-release (linux-bundle.yml).
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/clang-native.cmake")
set(VCPKG_C_FLAGS "-march=native")
set(VCPKG_CXX_FLAGS "-march=native")
