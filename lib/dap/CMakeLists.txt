# lib/dap/CMakeLists.txt

file(GLOB_RECURSE DAP_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(dap STATIC ${DAP_SOURCES})

target_include_directories(dap
    PUBLIC  ${CMAKE_SOURCE_DIR}/include       # target.h, notifier.h, server.h
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}       # dap.h (internal only)
)

target_compile_features(dap PUBLIC cxx_std_23)

target_link_libraries(dap PUBLIC nlohmann_json::nlohmann_json)
