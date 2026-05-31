# Collect all .cpp files in this directory and subdirectories for the executable
file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS *.cpp)

add_executable(udap ${SRC_FILES})
add_dependencies(udap vscode_ext)

# Place the binary in the top-level /bin folder
set_target_properties(udap PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)

# Link against the external and internal libraries
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

# Include directories for local project headers
target_include_directories(udap
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}              # <-- for dbg.h in src/
    ${CMAKE_CURRENT_SOURCE_DIR}/handlers     # optional, in case of local includes
    ${z80ex_SOURCE_DIR}/include              # z80ex headers
)
