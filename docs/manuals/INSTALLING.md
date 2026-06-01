# Installing μDAP

## Prerequisites

| Tool | Minimum version | Notes |
|------|----------------|-------|
| C++ compiler | GCC 13 / Clang 16 | C++23 required |
| CMake | 3.25 | |
| Git | any | for cloning and FetchContent |
| Node.js + npm | 18 | only needed to build the VSCode extension |
| Visual Studio Code | 1.88 | target editor |

All C++ library dependencies (nlohmann/json, sockpp, z80ex, structopt) are
fetched automatically by CMake the first time you build. You do not need to
install them separately.

---

## 1. Get the source

```sh
git clone https://github.com/retro-vault/udap.git
cd udap
```

No submodules — all dependencies are managed by CMake FetchContent.

---

## 2. Build

```sh
cmake -S . -B build
cmake --build build
```

The first build downloads dependencies; subsequent builds are fast.

Build outputs land in `bin/`:

```
bin/udap        ← the DAP server binary
bin/udap.vsix   ← the VSCode extension package
```

### Build options

| Option | Default | Effect |
|--------|---------|--------|
| `CMAKE_BUILD_TYPE` | `Debug` | Set to `Release` for an optimised binary |
| `BUILD_TESTS` | `ON` | Set to `OFF` to skip building the test suite |

Example release build:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

---

## 3. Run the tests

```sh
ctest --test-dir build --output-on-failure
```

All tests should pass before you proceed.

---

## 4. Install the VSCode extension

```sh
code --install-extension bin/udap.vsix
```

Verify it installed by opening the Extensions panel in VSCode and searching
for **μDAP Debugger**. Restart VSCode if prompted.

---

## 5. Configure your project

Add a debug configuration to `.vscode/launch.json` in the project you want
to debug:

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Debug with μDAP",
            "type": "udap",
            "request": "launch",
            "program": "${workspaceFolder}/build/my_program.ihx",
            "debugServer": 4711
        }
    ]
}
```

The `type` must be `"udap"`. The `debugServer` port must match the port the
server is listening on (default 4711).

### Optional launch properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `cdbFile` | string | `<program>.cdb` | Explicit path to SDCC CDB debug info |
| `mapFile` | string | `<program>.map` | Explicit path to linker MAP file |
| `sourceRoot` | string | directory of `program` | Root used to resolve relative source paths from the CDB |
| `sourceRoots` | string[] | `[]` | Additional source search roots |
| `includeRoots` | string[] | `[]` | Include directories also searched for source files |
| `startAddress` | number or string | from IHX record or `0x0000` | Override the program entry point |

Example with all options:

```json
{
    "name": "Debug Z80 project",
    "type": "udap",
    "request": "launch",
    "program": "${workspaceFolder}/out/main.ihx",
    "cdbFile": "${workspaceFolder}/out/main.cdb",
    "mapFile": "${workspaceFolder}/out/main.map",
    "sourceRoot": "${workspaceFolder}",
    "sourceRoots": [
        "${workspaceFolder}/src",
        "${workspaceFolder}/lib"
    ],
    "startAddress": "0x0100",
    "debugServer": 4711
}
```

---

## 6. Start a debug session

**Step 1** — start the DAP server in a terminal:

```sh
bin/udap
```

You should see:

```
DAP server listening on port 4711
```

The server stays running between sessions. Each time you press F5 in VSCode it
opens a new connection; when you stop debugging it closes the connection and
the server waits for the next one.

**Step 2** — press **F5** in VSCode with your project open. The debug session
starts.

---

## Troubleshooting

**`npx: command not found` during build**

Node.js is not installed or not on `PATH`. Install Node.js 18 or later from
[nodejs.org](https://nodejs.org), then re-run the CMake build.

**`bin/udap.vsix` is missing after build**

The extension packaging step requires `npx` to run `@vscode/vsce`. Check that
Node.js is installed and that the build step completed without errors:

```sh
cmake --build build 2>&1 | grep -i "ext\|vsix\|vsce"
```

**VSCode says "unable to connect to debug server"**

The server is not running, or it is running on a different port. Confirm:

```sh
ss -tlnp | grep 4711
```

If the port is busy from a previous run, kill the old process:

```sh
pkill udap
bin/udap
```

**Source files show as "unavailable"**

The CDB/MAP file contains relative paths that cannot be resolved. Set
`sourceRoot` in `launch.json` to the directory that contains your source
files, or add entries to `sourceRoots`.

**Breakpoints not binding ("unverified")**

The source line has no corresponding address in the CDB/MAP. This is normal
for blank lines, comments, and variable declarations. Try setting the
breakpoint on a line that contains an executable statement.
