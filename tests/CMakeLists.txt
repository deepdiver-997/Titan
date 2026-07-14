# Always fetch Catch2 v3 via FetchContent for consistency across platforms.
include(FetchContent)
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
)
FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

# ---- Test executable -------------------------------------------------------
add_executable(titan_tests
    main.cpp
    unit/recv_buffer.test.cpp
    unit/mailbox.test.cpp
    unit/timing_wheel.test.cpp
    unit/snapshot.test.cpp
    unit/mock_connection.test.cpp
    unit/recorder.test.cpp
    replay/deterministic_replay.test.cpp
)

target_link_libraries(titan_tests PRIVATE
    titan_core
    Catch2::Catch2
    Catch2::Catch2WithMain
)

# Enable debug-mode features (SnapshotManager, Recorder, capture macros).
target_compile_definitions(titan_tests PRIVATE TITAN_DEBUG)

# Register tests with CTest.
include(CTest)
include(Catch)
catch_discover_tests(titan_tests)
