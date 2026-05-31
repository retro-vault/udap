# tests/CMakeLists.txt

include(FetchContent)

# Fetch GoogleTest (v1.14.0 is the latest stable as of now)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)
FetchContent_MakeAvailable(googletest)

# Collect all test source files (both .cpp and .cxx)
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS *.cpp *.cxx)

# Define the test executable
add_executable(udap-tests ${TEST_SOURCES})

# Link with GoogleTest's main runner
target_link_libraries(udap-tests
    PRIVATE
        gtest_main
        dap
        sdcc
)

# Discover and register the tests for `ctest`
include(GoogleTest)
gtest_discover_tests(udap-tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
