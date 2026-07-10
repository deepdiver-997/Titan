# Example common library — Scene, SceneManager (AOI-based spatial simulation).
# NOT part of the core framework.

add_library(example_common STATIC
    scene/scene.cpp
    scene/scene_manager.cpp
)
target_include_directories(example_common PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    SYSTEM ${Boost_INCLUDE_DIRS}
)
target_link_libraries(example_common PUBLIC titan_core)
