cmake_minimum_required(VERSION 3.16)
project(titan VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Find dependencies ---------------------------------------------------
# Homebrew Boost 1.90+ ships BoostConfig without per-component config files,
# so prefer classic FindBoost module mode (Boost.Asio is header-only).
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 OLD)
endif()
set(Boost_NO_BOOST_CMAKE ON CACHE BOOL "Disable BoostConfig package mode" FORCE)
find_package(Boost REQUIRED)

# ---- Third-party: bthread_timer (brpc-style high-precision timer) --------
add_library(bthread_timer STATIC
    third_party/bthread_timer/timer.cpp
)
target_include_directories(bthread_timer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/bthread_timer
)
target_link_libraries(bthread_timer PUBLIC pthread)

# ---- Third-party: timing_wheel (skynet-style hierarchical wheel) ---------
add_library(timing_wheel STATIC
    third_party/timing_wheel/timing_wheel.cpp
)
target_include_directories(timing_wheel PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/timing_wheel
)

# ---- Titan game server core ----------------------------------------------
set(TITAN_CORE_SOURCES
    # Actor system
    src/gs/actor/mailbox.cpp
    src/gs/actor/actor.cpp
    src/gs/actor/actor_system.cpp
    # AOI system
    src/gs/aoi/aoi_entity.cpp
    src/gs/aoi/aoi_grid.cpp
    src/gs/aoi/aoi_world.cpp
    # Scene (optional framework module)
    src/gs/scene/scene.cpp
    src/gs/scene/scene_manager.cpp
    # Entity
    src/gs/entity/entity.cpp
    src/gs/entity/player.cpp
    src/gs/entity/npc.cpp
    # Network
    src/gs/net/tcp_server.cpp
    src/gs/net/tcp_connection.cpp
    src/gs/net/connection_manager.cpp
    src/gs/net/message.cpp
    src/gs/net/net_sync_actor.cpp
    # Peer-to-peer
    src/gs/net/tcp_peer.cpp
    src/gs/actor/peer_manager.cpp
    # Server
    src/gs/server/titan_server.cpp
)

add_library(titan_core STATIC ${TITAN_CORE_SOURCES})
target_include_directories(titan_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    SYSTEM ${Boost_INCLUDE_DIRS}
)
target_link_libraries(titan_core PUBLIC
    bthread_timer
    timing_wheel
)

# ---- Main executable -----------------------------------------------------
add_executable(titan_server
    src/gs/main.cpp
)
target_link_libraries(titan_server PRIVATE titan_core)

# ---- Example: tank battle server -----------------------------------------
add_subdirectory(examples/tank_battle)

# ---- Terminal clients (no graphics) -------------------------------------
add_executable(titan_client
    client/terminal_client.cpp
)
target_include_directories(titan_client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    SYSTEM ${Boost_INCLUDE_DIRS}
)

add_executable(terminal_tank
    client/src/terminal_tank.cpp
)
target_include_directories(terminal_tank PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    SYSTEM ${Boost_INCLUDE_DIRS}
)

# ---- Tank battle client (raylib) -----------------------------------------
option(BUILD_TANK_CLIENT "Build raylib tank battle client" ON)
if(BUILD_TANK_CLIENT)
    # Try brew-installed raylib first, fall back to FetchContent.
    find_package(raylib QUIET)
    if(NOT raylib_FOUND)
        include(FetchContent)
        FetchContent_Declare(raylib
            GIT_REPOSITORY https://github.com/raysan5/raylib.git
            GIT_TAG 5.5
        )
        FetchContent_MakeAvailable(raylib)
    endif()

    add_executable(titan_tank_client
        client/src/main.cpp
        client/src/game_client.cpp
        client/src/renderer.cpp
    )
    target_include_directories(titan_tank_client PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        SYSTEM ${Boost_INCLUDE_DIRS}
    )
    target_link_libraries(titan_tank_client PRIVATE raylib)
endif()
