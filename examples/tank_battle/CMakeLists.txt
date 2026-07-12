add_executable(titan_tank_server
    main.cpp
    battle_scene.cpp
    tank.cpp
    bullet.cpp
)
target_include_directories(titan_tank_server PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    SYSTEM ${Boost_INCLUDE_DIRS}
)
target_link_libraries(titan_tank_server PRIVATE titan_core)
