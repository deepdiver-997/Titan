add_executable(titan_gateway main.cpp)
target_include_directories(titan_gateway PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    SYSTEM ${Boost_INCLUDE_DIRS}
)
