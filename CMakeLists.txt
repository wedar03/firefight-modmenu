cmake_minimum_required(VERSION 3.14)
project(firefight_modmenu CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Dobby（inline hook 框架）----
set(DOBBY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/Dobby")
set(DOBBY_GENERATE_SHARED OFF CACHE BOOL "" FORCE)
set(DOBBY_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(DOBBY_BUILD_TEST OFF CACHE BOOL "" FORCE)
if(EXISTS "${DOBBY_DIR}/CMakeLists.txt")
    add_subdirectory(${DOBBY_DIR} dobby_build)
else()
    message(FATAL_ERROR "缺少 ${DOBBY_DIR}，请在 CI 里 checkout Dobby 或放到该目录")
endif()

# ---- 壳库：对外仍然叫 libSDL2.so ----
add_library(SDL2 SHARED
        src/main.cpp
        src/hooks.cpp
        src/ui.cpp
        src/text.cpp)

set_target_properties(SDL2 PROPERTIES OUTPUT_NAME "SDL2")

target_include_directories(SDL2 PRIVATE
        "${DOBBY_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(SDL2 dobby log android)

# 体积与符号：保留必要符号，去掉调试信息
target_compile_options(SDL2 PRIVATE -fvisibility=hidden -O2 -fPIC)
target_link_options(SDL2 PRIVATE -s)
