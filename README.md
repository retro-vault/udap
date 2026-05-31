![status.badge] [![language.badge]][language.url] [![standard.badge]][standard.url] [![license.badge]][license.url]

# μDAP (udap)

`μDAP (udap)` is a cross-debugging toolchain for Z80 programs, designed for integration with Visual Studio Code on Linux. It implements the Microsoft Debug Adapter Protocol (DAP) and serves as both a lightweight emulator backend and a debug adapter frontend.

> This project is used by the [mavrica](https://github.com/iskra-delta/mavrica)
  project (located in the same GitHub root), which uses a Z80 just-in-time (JIT) compilation core to emulate a Z80 system. `μDAP (udap)` enables debugging of such emulators directly from VSCode.

![Screenshot](docs/images/idp-emu-alpha.png)

## Features

- Z80 binary disassembly (full 64 KB listing, stable across steps)
- Debug Adapter Protocol implementation (fully VSCode compatible)
- Simple and clean C++23 implementation
- Instruction and memory-level debugging
- Register inspection (with subsystem trees like CPU, CTC, etc.)
- C source-level debugging via SDCC CDB and MAP files
- C local variable display with live values (stack and register variables)
- C global variable display (data variables only, functions filtered out)
- Breakpoints in C source, assembly source files, and the disassembly view
- Source-level step over, step into, and step out
- Block instruction support — LDIR, LDDR, CPIR, CPDR, INIR, INDR, OTIR, OTDR treated as single atomic steps
- Symbol substitution in disassembly (addresses replaced with MAP symbol names)

## Design goals

- Portable, standard-compliant C++ with minimal dependencies
- Clear separation between emulator core and DAP frontend
- Easy to embed, reuse, or modify
- Source-level and binary-level debugging support

## Build instructions

```sh
git clone https://github.com/retro-vault/udap.git --recurse-submodules
cd udap
cmake -S . -B build
cmake --build build
```

To run the tests:

```sh
ctest --test-dir build --output-on-failure
```

The build produces two key outputs:

- `bin/udap` — the debug adapter binary
- `bin/udap.vsix` — the Visual Studio Code extension you can install with:

```sh
code --install-extension bin/udap.vsix
```

## VSCode integration

Add the following to your `.vscode/launch.json` in your project (e.g. in `mavrica`) to enable debugging:

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "Debug mavrica (Z80 JIT DAP)",
      "type": "udap",
      "request": "launch",
      "program": "${workspaceFolder}/build/mavrica.bin",
      "preLaunchTask": "Build mavrica",
      "debugServer": 4711
    }
  ]
}
```

> **Note:** The `type` field must be `"udap"` — not `"idp-gdb"` or any other value.

Optional adapter-specific launch arguments:

- `sourceRoot`: single root folder used to resolve source files referenced by CDB.
- `sourceRoots`: array of fallback root folders for source resolution.
- `includeRoots`: array of include search roots (also used for source file resolution).
- `cdbFile`: explicit path to CDB file (default is `<program>.cdb`).
- `mapFile`: explicit path to MAP file (default is `<program>.map`).
- `startAddress`: explicit program entry point (number or string like `"0x1234"`).
  If omitted, the IHX start address record is used when available; otherwise entry defaults to `0x0000`.

## Debugging workflow

### Starting a session

1. Start the DAP server in a terminal:
   ```sh
   bin/udap
   ```
   It will print `DAP server listening on port 4711` and wait.

2. Open your project in VSCode and press **F5**. The extension connects to port 4711,
   sends `initialize` → `launch`, and the debug session starts.

### Source-level debugging (with CDB/MAP)

When the program is compiled with SDCC debug information:
- **Variables panel** shows live C local variables (stack and register-allocated) and global data variables.
- **Step Over (F10)** advances one C source line, transparently stepping over function prologues and call-setup code.
- **Step Into (F11)** steps into the next C source line, including into called functions, skipping over call-setup assembly and function prologues automatically.
- **Step Out (Shift+F11)** runs until the current function returns.
- **Breakpoints** can be set in `.c` and `.s`/`.asm` source files before or after launch; they are resolved to addresses via CDB and MAP data.

### Assembly-level debugging (without CDB/MAP, or in mixed code)

When no C source mapping is available for the current address, the debugger falls back to a full 64 KB disassembly view:
- The entire Z80 address space is disassembled once at launch and shown as a stable virtual source file (`z80.s`).
- Stepping scrolls to the current instruction without resetting the view — you can freely scroll to inspect surrounding code.
- **Breakpoints can be set directly in the disassembly view** by clicking the gutter.
- Hex addresses in the listing are automatically replaced with MAP symbol names where available.
- Block instructions (`LDIR`, `LDDR`, `CPIR`, `CPDR`, `INIR`, `INDR`, `OTIR`, `OTDR`) execute atomically — a single step completes the entire block transfer.
- Library function calls with no source mapping are transparently stepped over when using source-level step commands.

### C variable display

The Variables panel shows three scopes when CDB data is loaded:

| Scope | Contents |
|---|---|
| **Registers** | Z80 CPU registers (AF, BC, DE, HL, IX, IY, SP, PC, R, I, F) with memory references |
| **Locals** | Local variables of the current function with live values |
| **Globals** | Global C data variables with live values (functions excluded) |

Local variable values are read from:
- Stack variables (IX-relative): `memory[IX + offset]`
- Register variables: the named register (e.g. `[c]` → register C, `[c,b]` → BC)
- Static/extern variables: looked up by symbol name in the MAP

## Directory structure

- `src/` — main entry point and DAP TCP server
- `src/handlers/` — one handler per DAP command (chain of responsibility)
- `lib/dap/` — Debug Adapter Protocol message parser/serializer
- `lib/sdcc/` — SDCC CDB and MAP file parsers
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

- DAP message parsing and dispatching
- DAP to emulator interface
- Full 64 KB disassembly view with symbol substitution
- Register view with CPU tree
- Visual Studio Code extension integration (`type: udap`)
- Instruction breakpoints (by address)
- Source breakpoints in C files (via CDB/MAP)
- Breakpoints in assembly source files (via CDB `A$` line records)
- Breakpoints in the disassembly view (gutter click)
- Continue / step over / step into / step out (source-level and instruction-level)
- Source-level stepping skips function prologues and call-setup code automatically
- Block instruction atomicity (LDIR etc. complete in one step)
- C source line mapping and source delivery via `sourceReference`
- MAP parser integration (segments/symbols + symbolized stack fallback)
- C local variable display (stack and register-allocated)
- C global variable display (data variables; function declarations excluded)
- Assembly line-to-address mapping for `.s` files without an `M:` CDB record (e.g. `crt0.s`)

### In development

- Watchpoints
- Restart / terminate semantics improvements

### Planned

- Expression/evaluate improvements (compound expressions, memory reads)
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
