cmake_minimum_required(VERSION 3.20)
project(promptemon_steidium LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Rumble World resource/model decoding retained only so the C&C-based project
# can consume the installed stage assets.
add_library(promptemon_rom
    src/rom/Lzh8.cpp
    src/rom/Lz11.cpp
    src/rom/RomInspector.cpp
    src/rom/RomLibrary.cpp
    src/rom/RomModelArchive.cpp
    src/rom/CiaBchModelSource.cpp
    src/rom/RumbleWorldArchive.cpp
)
target_include_directories(promptemon_rom PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(promptemon_bch
    src/bch/BchScene.cpp
    src/ctr/CtrTexture.cpp
)
target_include_directories(promptemon_bch PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(WIN32)
    add_executable(promptemon_demo WIN32
        src/demo/StageSceneWin32.cpp
        src/storage/RomAssetCache.cpp
        src/storage/RomAssetCacheInstalled.cpp
    )
    target_compile_definitions(promptemon_demo PRIVATE NOMINMAX UNICODE _UNICODE)
    if(MSVC)
        target_compile_options(promptemon_demo PRIVATE /utf-8)
    endif()
    target_link_libraries(promptemon_demo PRIVATE
        promptemon_rom
        promptemon_bch
        opengl32
        gdi32
        user32
        shell32
    )
endif()
