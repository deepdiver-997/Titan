find_package(Catch2 REQUIRED)

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
