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
    # Compile and use the original Command & Conquer Generals UDP transport.
    # Compatibility headers only provide the small engine typedef/macro surface
    # that udp.cpp expected from the original Generals build.
    add_library(promptemon_cnc_udp
        third_party/cnc/network/src/GameNetwork/udp.cpp
        src/net/CncUdpTransport.cpp
    )
    target_include_directories(promptemon_cnc_udp
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cnc/network/include
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cnc/network/compat
    )
    target_compile_definitions(promptemon_cnc_udp PRIVATE
        _WINDOWS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    if(MSVC)
        target_compile_options(promptemon_cnc_udp PRIVATE /utf-8)
    endif()
    target_link_libraries(promptemon_cnc_udp PUBLIC ws2_32)

    # Promptemon session/discovery layer carried over the original C&C UDP socket.
    add_library(promptemon_cnc_lan
        src/net/CncLanSession.cpp
    )
    target_include_directories(promptemon_cnc_lan PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(promptemon_cnc_lan PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(promptemon_cnc_lan PRIVATE /utf-8)
    endif()
    target_link_libraries(promptemon_cnc_lan PUBLIC promptemon_cnc_udp)

    add_library(promptemon_demo_core
        src/demo/SceneRenderSupport.cpp
        src/demo/CameraController.cpp
        src/demo/StageWorld.cpp
        src/demo/PokemonActor.cpp
        src/demo/JumpController.cpp
        src/storage/RomAssetCache.cpp
        src/storage/RomAssetCacheInstalled.cpp
    )
    target_include_directories(promptemon_demo_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(promptemon_demo_core PUBLIC NOMINMAX UNICODE _UNICODE)
    if(MSVC)
        target_compile_options(promptemon_demo_core PRIVATE /utf-8)
    endif()
    target_link_libraries(promptemon_demo_core PUBLIC
        promptemon_rom
        promptemon_bch
        opengl32
        glu32
        gdi32
        user32
        shell32
    )

    add_executable(promptemon_demo WIN32
        src/demo/StageSceneWin32.cpp
    )
    target_compile_definitions(promptemon_demo PRIVATE NOMINMAX UNICODE _UNICODE)
    if(MSVC)
        target_compile_options(promptemon_demo PRIVATE /utf-8)
    endif()
    target_link_libraries(promptemon_demo PRIVATE promptemon_demo_core)

    # Alternative executable: enlarged in-stadium view, local Pokemon exactly
    # at arena center, left-click jump and automatic LAN peers over C&C UDP.
    add_executable(promptemon_stage_jump WIN32
        src/demo/StageJumpDemoWin32.cpp
    )
    target_compile_definitions(promptemon_stage_jump PRIVATE NOMINMAX UNICODE _UNICODE)
    if(MSVC)
        target_compile_options(promptemon_stage_jump PRIVATE /utf-8)
    endif()
    target_link_libraries(promptemon_stage_jump PRIVATE promptemon_demo_core promptemon_cnc_lan)

    add_executable(promptemon_lan_smoke
        tools/promptemon_lan_smoke.cpp
    )
    target_compile_definitions(promptemon_lan_smoke PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(promptemon_lan_smoke PRIVATE /utf-8)
    endif()
    target_link_libraries(promptemon_lan_smoke PRIVATE promptemon_cnc_lan)
endif()
