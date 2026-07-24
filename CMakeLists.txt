cmake_minimum_required(VERSION 3.28)
project(Game LANGUAGES CXX)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)

include(FetchContent)
FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 6.0                  
    GIT_SHALLOW ON              
)
FetchContent_MakeAvailable(raylib)

# Enable raylib's GPU skinning path. Without this, raylib's config.h defaults
# SUPPORT_GPU_SKINNING to 0, so it never uploads per-vertex bone index/weight
# VBOs — a skinning shader then skins every vertex by bone 0, freezing the mesh
# in its bind pose (T-pose) while only root motion animates. config.h guards the
# flag with #ifndef, so this compile definition wins.
target_compile_definitions(raylib PUBLIC SUPPORT_GPU_SKINNING=1)

file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS src/*.cpp)
file(GLOB_RECURSE HEADER_FILES CONFIGURE_DEPENDS include/*.h)

add_executable(${PROJECT_NAME} ${SRC_FILES} ${HEADER_FILES})

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)

target_link_libraries(${PROJECT_NAME} PRIVATE raylib)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_definitions(${PROJECT_NAME} PRIVATE ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")