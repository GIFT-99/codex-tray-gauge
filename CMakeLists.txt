cmake_minimum_required(VERSION 3.16)
project(codex-tray-gauge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# nlohmann/json — single-header JSON (MIT license)
include(FetchContent)
FetchContent_Declare(json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

add_executable(codex-tray-gauge WIN32
  src/main.cpp
  src/tray_app.cpp
  src/codex_client.cpp
  src/quota_model.cpp
  src/icon_renderer.cpp
  src/resource.rc
)
target_include_directories(codex-tray-gauge PRIVATE src)
target_link_libraries(codex-tray-gauge PRIVATE gdiplus nlohmann_json::nlohmann_json)

# Static link MinGW runtime libraries (no DLL dependencies)
if(MINGW)
  target_link_options(codex-tray-gauge PRIVATE -static)
endif()

# Generate debug symbols even in Release
if(MSVC)
  target_compile_options(codex-tray-gauge PRIVATE /W4 /utf-8)
endif()
