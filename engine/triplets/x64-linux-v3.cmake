# Sandbox bundle build for CPUs with AVX2 (x86-64-v3: Haswell, Zen and later), so Skia uses its AVX2
# pipeline. Shipped next to the baseline build; skill/bin/llm-video-engine picks one at run time.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_C_FLAGS "-march=x86-64-v3")
set(VCPKG_CXX_FLAGS "-march=x86-64-v3")
