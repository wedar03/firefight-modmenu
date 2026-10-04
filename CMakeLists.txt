cmake_minimum_required(VERSION 3.14)
project(firefight_modmenu CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# hook 用自写的 GOT/PLT 实现（src/gothook.cpp），不依赖任何第三方框架

# ---- 壳库：对外仍然叫 libSDL2.so ----
add_library(SDL2 SHARED
        src/main.cpp
        src/hooks.cpp
        src/ui.cpp
        src/text.cpp
        src/gothook.cpp)

set_target_properties(SDL2 PROPERTIES OUTPUT_NAME "SDL2")

target_include_directories(SDL2 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(SDL2 log android)

target_compile_options(SDL2 PRIVATE -fvisibility=hidden -O2 -fPIC)
target_link_options(SDL2 PRIVATE -s)
