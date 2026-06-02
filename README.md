![status.badge] [![language.badge]][language.url] [![standard.badge]][standard.url] [![license.badge]][license.url]

# μDAP (udap)

`μDAP (udap)` is a cross-debugging toolchain for Z80 programs, designed for
integration with Visual Studio Code on Linux. It implements the Microsoft Debug
Adapter Protocol (DAP) and provides both a lightweight Z80 emulator backend
and a reusable library for adding DAP debugging to any emulator.

![Screenshot](docs/images/idp-emu-alpha.png)

---

## Features

### Source-level C debugging

- Step over, step into, and step out at the C source line level via SDCC CDB files
- Live local variable display — stack-allocated (IX-relative) and register-allocated
- Live global variable display — data variables with values read from emulated memory
- Breakpoints in C source files, resolved to addresses via CDB and MAP

### Assembly debugging

- Full 64 KB disassembly view generated at launch and stable across all steps
- Breakpoints directly in the disassembly gutter
- Hex addresses replaced with MAP symbol names throughout the listing
- Block instructions (LDIR, LDDR, CPIR, CPDR, INIR, INDR, OTIR, OTDR) treated as a single atomic step

### Mixed C and assembly

- Breakpoints in `.s` / `.asm` source files with gutter support in VSCode
- Step over transparently skips function prologues and call-setup code
- Step into enters assembly functions from C source and back

### Register and memory inspection

- CPU registers (AF, BC, DE, HL, IX, IY, SP, PC, R, I, F) with memory references
- MAP segments and symbols as browsable variable scopes
- Memory panel — read any address range directly from emulated memory
- Expression evaluation — register names, MAP symbols, hex addresses

### Debugger capabilities

- Instruction breakpoints by address
- Function breakpoints by name (via MAP symbols)
- Step back — returns the emulator to its state before the last step
- `sourceReference` delivery for files not on disk (generated listings, ROM sources)

---

## Getting started

See **[docs/manuals/INSTALLING.md](docs/manuals/INSTALLING.md)** for build
instructions, extension installation, and `launch.json` configuration.

---

## Using the library in your own emulator

μDAP ships as a library (`libdap`) so you can add DAP debugging to any
emulator. The entire public API is one header:

```cpp
#include <dap/target.h>
```

Subclass `dap::target`, implement the virtual methods for your hardware, and
call `run()`:

```cpp
class my_target : public dap::target {
public:
    bool launch(const dap::launch_args &args) override { ... }
    void resume()  override { ... }   // start emulation thread
    void step()    override { ... }   // single step, synchronous
    // ...
};

int main() {
    my_target t;
    t.run(std::cin, std::cout);   // runs the DAP session
}
```

When your emulation loop hits a breakpoint on a background thread, call
`stopped()` — it is thread-safe and can be called from any thread:

```cpp
// from your emulation thread:
stopped("breakpoint");
```

See **[docs/manuals/USING-LIBDAP.md](docs/manuals/USING-LIBDAP.md)** for the
full method reference and multi-threading guide.

---

## Example application

[udap-app](https://github.com/retro-vault/udap-app) is a minimal Z80 C
application (a prime sieve with assembly startup code) built to validate the
full debugger stack — SDCC metadata, linker MAP, CDB symbol loading, and mixed
C/assembly stepping. Use it to verify your installation or as a reference for
your own `launch.json` configuration.

---

## Directory structure

| Directory | Contents |
|-----------|----------|
| `src/` | DAP TCP server and Z80 target implementation |
| `lib/dap/` | DAP protocol library — the part you reuse |
| `lib/sdcc/` | SDCC CDB and MAP file parsers |
| `include/dap/` | Public headers (`target.h`) |
| `tests/` | GoogleTest unit tests |
| `ext/` | VSCode extension source and packaging |
| `docs/manuals/` | User and developer documentation |

---

## Dependencies

All fetched automatically by CMake — nothing to install manually.

| Library | Purpose |
|---------|---------|
| [nlohmann/json](https://github.com/nlohmann/json) | JSON parsing (header-only) |
| [sockpp](https://github.com/fpagliughi/sockpp) | TCP socket abstraction |
| [z80ex](https://github.com/lipro/z80ex) | Z80 CPU emulator core |
| [structopt](https://github.com/p-ranav/structopt) | Command-line option parsing |

---

## Build

```sh
git clone https://github.com/retro-vault/udap.git
cd udap
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Install the VSCode extension:

```sh
code --install-extension bin/udap.vsix
```

---

## License

MIT. See [LICENSE](LICENSE).

## Copyright

Copyright © 2025, 2026 Tomaz Stih. All rights reserved.

[language.url]:   https://en.wikipedia.org/wiki/C%2B%2B23%2B
[language.badge]: https://img.shields.io/badge/language-C++-blue.svg
[standard.url]:   https://en.cppreference.com/w/cpp/23
[standard.badge]: https://img.shields.io/badge/standard-C++23-blue.svg
[license.url]:    https://github.com/tstih/libcpm3-z80/blob/main/LICENSE
[license.badge]:  https://img.shields.io/badge/license-MIT-blue.svg
[status.badge]:   https://img.shields.io/badge/status-beta-orange.svg
