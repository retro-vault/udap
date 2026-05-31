![status.badge] [![language.badge]][language.url] [![standard.badge]][standard.url] [![license.badge]][license.url]

# μDAP (udap)

`μDAP (udap)` is a cross-debugging toolchain for Z80 programs, designed for integration with Visual Studio Code on Linux. It implements the Microsoft Debug Adapter Protocol (DAP) and serves as both a lightweight emulator backend and a debug adapter frontend.

 > This project is used by the [mavrica](https://github.com/iskra-delta/mavrica)
   project (located in the same GitHub root), which uses a Z80 just-in-time (JIT) compilation core to emulate a Z80 system. `μDAP (mudap)` enables debugging of such emulators directly from VSCode.

![Screenshot](docs/images/idp-emu-alpha.png)

## Features

- Z80 binary disassembly (no source required)
- Debug Adapter Protocol implementation (fully VSCode compatible)
- Simple and clean C++23 implementation
- Instruction and memory-level debugging
- Register inspection (with subsystem trees like CPU, CTC, etc.)

## Design goals

- Portable, standard-compliant C++ with minimal dependencies
- Clear separation between emulator core and DAP frontend
- Easy to embed, reuse, or modify
- Source-level and binary-level debugging support

## Build instructions

```sh
git clone https://github.com/iskra-delta/mudap.git --recurse-submodules
cd mudap
cmake -S . -B build
cmake --build build
```

To run the tests:

```sh
ctest --test-dir build --output-on-failure
```

The build produces two key outputs:

- `bin/mudap` — the debug adapter binary
- `bin/mudap.vsix` — the Visual Studio Code extension you can install with:

```sh
code --install-extension bin/mudap.vsix
```

## VSCode integration

Add the following to your `.vscode/launch.json` in your project (e.g. in `mavrica`) to enable debugging:

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "Debug mavrica (Z80 JIT DAP)",
      "type": "mudap",
      "request": "launch",
      "program": "${workspaceFolder}/build/mavrica.bin",
      "preLaunchTask": "Build mavrica",
      "debugServer": 4711,
      "showDisassembly": "always"
    }
  ]
}
```

Optional adapter-specific launch arguments:

- `sourceRoot`: single root folder used to resolve source files referenced by CDB.
- `sourceRoots`: array of fallback root folders for source resolution.
- `includeRoots`: array of include search roots (also used for source file resolution).
- `cdbFile`: explicit path to CDB file (default is `<program>.cdb`).
- `mapFile`: explicit path to MAP file (default is `<program>.map`).
- `startAddress`: explicit program entry point (number or string like `"0x1234"`).
  If omitted, IHX start address is used when available; otherwise entry defaults to `0x0000`.

## Directory structure

- `src/` — main entry point and DAP TCP server
- `lib/dap/` — Debug Adapter Protocol message parser/serializer
- `lib/` — reusable internal components (emulation, memory, etc.)
- `include/` — public headers
- `tests/` — unit tests using GoogleTest
- `ext/` — Visual Studio Code extension source
- `docs/` — additional documentation

## Dependencies

Handled automatically using `FetchContent` in CMake:

- `nlohmann/json` — JSON parser (header-only)
- `sockpp` — TCP socket abstraction
- `structopt` — command-line option parsing
- `z80ex` — Z80 CPU emulator core
- `z80ex_dasm` — Z80 disassembler

## Status

This project is under active development. Current component status:

### Complete

- DAP message parsing
- DAP to emulator interface
- Disassembler output
- Register view with CPU tree
- Visual Studio Code extension integration (`type: mudap`)
- Instruction breakpoints
- Continue / step (`next`, `stepIn`, `stepOut`)
- Source code integration via CDB + MAP fallback
- C source line mapping and source delivery via `sourceReference`
- MAP parser integration (segments/symbols + symbolized stack fallback)

### In development

- Watchpoints
- Restart / terminate semantics improvements
- Broader debug metadata ingestion (ADB/NOI/SYM)

### Planned

- Expression/evaluate support
- Memory/segment UX improvements using MAP metadata

### Nice to have

- Platform support plugins (Iskra Delta Partner...)
- Custom Visual Studio Code views

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.

## Copyright

Copyright © 2025, 2026 Tomaz Stih  
All rights reserved.

[language.url]: https://en.wikipedia.org/wiki/C%2B%2B23%2B
[language.badge]: https://img.shields.io/badge/language-C++-blue.svg
[standard.url]: https://en.cppreference.com/w/cpp/23
[standard.badge]: https://img.shields.io/badge/standard-C++23-blue.svg
[license.url]: https://github.com/tstih/libcpm3-z80/blob/main/LICENSE
[license.badge]: https://img.shields.io/badge/license-MIT-blue.svg
[status.badge]: https://img.shields.io/badge/status-alpha-orange.svg
