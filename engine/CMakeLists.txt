cmake_minimum_required(VERSION 3.25)
project(llm_video_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(unofficial-skia CONFIG REQUIRED)
find_package(harfbuzz CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(llm-video-engine
  src/main.cpp
  src/render/graph.cpp
  src/render/image.cpp
  src/render/text.cpp
  src/scenes/frame.cpp
  src/scenes/components.cpp
  src/encode/pipe.cpp
)
target_include_directories(llm-video-engine PRIVATE src)
target_link_libraries(llm-video-engine PRIVATE
  unofficial::skia::skia
  unofficial::skia::modules::svg
  harfbuzz::harfbuzz
  nlohmann_json::nlohmann_json
)
target_compile_definitions(llm-video-engine PRIVATE
  LVE_DEFAULT_FONT="${CMAKE_CURRENT_SOURCE_DIR}/../themes/default/Montserrat-VF.ttf")
if(MSVC)
  target_compile_options(llm-video-engine PRIVATE /W3 /utf-8 /permissive-)
  target_compile_definitions(llm-video-engine PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
endif()
