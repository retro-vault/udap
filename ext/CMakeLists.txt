set(EXT_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(EXT_BUILD_DIR "${CMAKE_BINARY_DIR}/ext")
set(EXT_VSIX "${CMAKE_SOURCE_DIR}/bin/udap.vsix")

# List source files required for VS Code extension packaging.
set(EXT_FILES
    "${EXT_SRC_DIR}/package.json"
    "${EXT_SRC_DIR}/LICENSE"
)

# Stage extension sources into build/ext.
add_custom_target(prepare_extension ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${EXT_BUILD_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "${EXT_SRC_DIR}/package.json" "${EXT_BUILD_DIR}/package.json"
    COMMAND ${CMAKE_COMMAND} -E copy "${EXT_SRC_DIR}/LICENSE" "${EXT_BUILD_DIR}/LICENSE"
    DEPENDS ${EXT_FILES}
    COMMENT "Staging extension sources to build/ext"
)

add_custom_command(
    OUTPUT "${EXT_VSIX}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/bin"
    COMMAND npx --yes @vscode/vsce package --no-dependencies --allow-missing-repository --out "${EXT_VSIX}"
    WORKING_DIRECTORY "${EXT_BUILD_DIR}"
    DEPENDS prepare_extension
    COMMENT "Packaging VS Code extension with @vscode/vsce"
)

add_custom_target(vscode_ext
    DEPENDS "${EXT_VSIX}"
    COMMENT "VS Code extension .vsix built in bin/"
)
