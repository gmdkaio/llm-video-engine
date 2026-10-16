# Toolchain for x64-linux-native: vcpkg's Linux toolchain with clang.
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
include("$ENV{VCPKG_ROOT}/scripts/toolchains/linux.cmake")
