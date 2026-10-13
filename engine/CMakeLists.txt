cmake_minimum_required(VERSION 3.25)
project(llm_video_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(unofficial-skia CONFIG REQUIRED)
find_package(harfbuzz CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(spike
  src/spike_main.cpp
  src/render/graph.cpp
  src/render/text.cpp
  src/scenes/kpi.cpp
  src/encode/pipe.cpp
)
target_include_directories(spike PRIVATE src)
target_link_libraries(spike PRIVATE
  unofficial::skia::skia
  harfbuzz::harfbuzz
  nlohmann_json::nlohmann_json
)
if(MSVC)
  target_compile_options(spike PRIVATE /W3 /utf-8 /permissive-)
  target_compile_definitions(spike PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()
