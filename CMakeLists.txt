cmake_minimum_required(VERSION 3.20)
project(promptemon_steidium LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Rumble World resource/model decoding retained so the C&C-based project can
# consume the installed stage assets and build the resource bundle installer.
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
    src/ctr/Bclim.cpp
)
target_include_directories(promptemon_bch PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Resource packager kept as part of the project. v3 scans resources-tx and
# creates the self-extracting resource bundle using the SFX stub.
add_executable(promptemon_resource_packager
    tools/promptemon_resource_packager_v3.cpp
)
target_link_libraries(promptemon_resource_packager PRIVATE promptemon_rom promptemon_bch)

add_executable(promptemon_resource_sfx
    tools/promptemon_resource_sfx.cpp
)

if(MSVC)
    target_compile_options(promptemon_resource_packager PRIVATE /utf-8)
    target_compile_options(promptemon_resource_sfx PRIVATE /utf-8)
endif()

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
