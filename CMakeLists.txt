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

# Fetch RecastNavigation
set(RECASTNAVIGATION_DEMO OFF CACHE BOOL "" FORCE)
set(RECASTNAVIGATION_TESTS OFF CACHE BOOL "" FORCE)
set(RECASTNAVIGATION_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(recastnavigation
    GIT_REPOSITORY https://github.com/recastnavigation/recastnavigation.git
    GIT_TAG main
    GIT_SHALLOW ON
)
# Fix for MinGW/GCC missing INTPTR_MAX in RecastAlloc.h
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_definitions(INTPTR_MAX=__INTPTR_MAX__)
endif()

FetchContent_MakeAvailable(recastnavigation)

# Level files are JSON. Fetched from the packaged release archive rather than
# the git repo: the repo carries a large test-data suite that FetchContent would
# clone in full, while json.tar.xz is the headers plus a CMake package and
# nothing else.
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
)
FetchContent_MakeAvailable(nlohmann_json)

# Enable raylib's GPU skinning path. Without this, raylib's config.h defaults
# SUPPORT_GPU_SKINNING to 0, so it never uploads per-vertex bone index/weight
# VBOs — a skinning shader then skins every vertex by bone 0, freezing the mesh
# in its bind pose (T-pose) while only root motion animates. config.h guards the
# flag with #ifndef, so this compile definition wins.
target_compile_definitions(raylib PUBLIC SUPPORT_GPU_SKINNING=1)

# Let raylib decode the JPEG textures embedded in a level .glb. config.h defaults
# SUPPORT_FILEFORMAT_JPG to 0, so stb_image is compiled without its JPEG decoder
# and every jpeg-mimed image in a glTF fails with "Data format not supported" —
# the model still loads, just untextured, which reads as a broken material rather
# than a missing codec. Blender's exporter picks JPEG on its own for any texture
# that has no alpha to preserve (tools/export_level.py exports AUTO), so this is
# reached by any kitbashed map with art on it.
target_compile_definitions(raylib PUBLIC SUPPORT_FILEFORMAT_JPG=1)

file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS src/*.cpp)
file(GLOB_RECURSE HEADER_FILES CONFIGURE_DEPENDS include/*.h)

add_executable(${PROJECT_NAME} ${SRC_FILES} ${HEADER_FILES})

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)

target_link_libraries(${PROJECT_NAME} PRIVATE raylib Recast Detour nlohmann_json::nlohmann_json)

# RecastNavigation compiles itself with -fno-rtti, so it never emits typeinfo
# for rcContext. NavMeshBuilder.cpp derives BuildContext from it; under the
# Itanium ABI (Clang/GCC) typeinfo for a polymorphic class lives only in the TU
# defining its key function, so the derived typeinfo we emit here references a
# base typeinfo that does not exist anywhere -> undefined symbol at link.
# MSVC emits type descriptors per-TU as COMDATs, which is why Windows links
# fine and only macOS/Linux hit this. BuildContext is file-local, so dropping
# RTTI for just this source is safe and matches the ABI Recast was built with.
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(src/AI/NavMeshBuilder.cpp PROPERTIES COMPILE_OPTIONS -fno-rtti)
endif()

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# The asset root is found at RUNTIME, relative to the executable — see
# src/Core/AssetPaths.cpp. This is only the last-resort fallback it uses when
# there is no assets directory beside the binary at all, which keeps a
# developer's build runnable from anywhere on this machine. A packaged copy
# never reaches it, because the symlink below means one is always beside it.
target_compile_definitions(${PROJECT_NAME} PRIVATE SOURCE_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")

# Put an `assets` beside the binary, so running out of the build tree exercises
# the same lookup a player's copy will. Without it the build-tree run would only
# ever hit the fallback above, and the path that actually ships would never be
# tested until someone else ran it.
#
# A symlink rather than a copy: the assets are 140 MB and change constantly.
if (WIN32)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/assets"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets"
        COMMENT "Copying assets next to the executable")
else()
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink
                "${CMAKE_SOURCE_DIR}/assets"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets"
        COMMENT "Linking assets next to the executable")
endif()