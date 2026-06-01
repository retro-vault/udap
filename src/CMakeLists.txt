add_executable(udap
    main.cpp
    z80_target.cpp
    dbg.cpp
    emulator.cpp
    socket_stream.cpp
)
add_dependencies(udap vscode_ext)

set_target_properties(udap PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)

target_link_libraries(udap
  PRIVATE
    sockpp
    structopt::structopt
    nlohmann_json::nlohmann_json
    dap
    sdcc
    z80ex
    z80ex_dasm
)

target_include_directories(udap
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${z80ex_SOURCE_DIR}/include
)
