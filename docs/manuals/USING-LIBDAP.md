# Using libdap

libdap implements the [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/)
server side. You give it one class that describes your emulator; it handles all
the DAP ceremony — JSON framing, capability negotiation, source reference
management, scope indexing — and calls your class when VSCode wants to step,
inspect memory, set breakpoints, and so on.

## The only file you need

```cpp
#include <dap/target.h>
```

That is the entire public API.

---

## Implementing a target

Subclass `dap::target` and override the virtual methods that apply to your
hardware. Methods you do not override return safe defaults (empty lists, nullopt,
or no-ops).

```cpp
class my_target : public dap::target {
public:
    // --- required ---
    bool launch(const dap::launch_args &args) override;
    void resume()   override;
    void pause()    override;
    void step()     override;
    void step_in()  override;
    void step_out() override;

    std::vector<uint8_t>             read_memory(uint16_t addr, int count)       const override;
    std::vector<dap::frame_info>     get_stack()                                 const override;
    std::vector<dap::scope_info>     get_scopes()                                const override;
    std::vector<dap::variable_info>  get_variables(const std::string &scope)     const override;

    std::vector<dap::breakpoint_info> set_source_breakpoints(
        const std::string &path, int source_reference,
        const std::vector<int> &lines) override;

    dap::eval_info evaluate(const std::string &expr) const override;
};
```

Then run the session on whatever streams connect you to VSCode:

```cpp
int main() {
    my_target t;
    t.run(std::cin, std::cout);  // blocks until client disconnects
}
```

---

## Data types

All types are plain structs with no behaviour of their own.

| Struct | Returned by | What to fill in |
|--------|-------------|-----------------|
| `launch_args` | passed to `launch()` | program path, debug files, source roots |
| `frame_info` | `get_stack()` | current PC (hex), source file path, line |
| `scope_info` | `get_scopes()` | display name, hint (`"registers"` / `"locals"`) |
| `variable_info` | `get_variables()` | name, value string, optional memory address |
| `breakpoint_info` | `set_*_breakpoints()` | verified flag, line, optional instruction ref |
| `source_info` | `get_source()` | file content string, MIME type |
| `disasm_info` | `disassemble()` | address, bytes, mnemonic, optional source location |
| `eval_info` | `evaluate()` | result string, optional memory address |

---

## Method reference

### `launch(args)`

Called once when the client starts a debug session. Load your binary, reset the
CPU, parse any debug info. Return `true` on success.

```cpp
bool my_target::launch(const dap::launch_args &args) {
    cpu_.reset();
    load_binary(args.program);
    return true;
}
```

`launch_args` fields:

| Field | Meaning |
|-------|---------|
| `program` | path to the binary (from `launch.json`) |
| `cdb_file` | optional SDCC CDB debug info path |
| `map_file` | optional linker MAP file path |
| `source_root` | primary root for resolving relative source paths |
| `source_roots` | additional search roots |
| `start_address` | optional PC override at entry |

**Important:** set `source_root`/`source_roots` in your launch implementation
**before** parsing debug info (CDB/MAP). The debug info parser calls
`rebuild_source_index()` immediately, and it needs the roots to resolve
relative paths. Paths resolved at index-build time are used later for stepping
and breakpoint matching.

### `disconnect()`

Optional. Called when the client disconnects. Stop any background threads,
release resources.

### `resume()`

Start running the emulator. **This must return immediately.** Run the emulation
on a background thread and call `stopped()` when it halts (breakpoint, pause,
HALT instruction, etc.). See [multi-threading](#multi-threading) below.

```cpp
void my_target::resume() {
    running_.store(true);
    emulation_thread_ = std::thread([this] {
        while (running_.load()) {
            cpu_.step();
            if (is_breakpoint(cpu_.pc())) {
                running_.store(false);
                stopped("breakpoint");   // <-- call on this thread, safe
                break;
            }
        }
    });
}
```

### `pause()`

Signal the background emulation thread to stop. Set an atomic flag; the
emulation loop checks it. Do not call `stopped()` here — the emulation loop
calls it after seeing the flag.

```cpp
void my_target::pause() {
    pause_requested_.store(true);
}
```

### `step()`, `step_in()`, `step_out()`, `step_back()`

Execute one logical step **synchronously** (these block). After they return,
libdap fires the `stopped` event automatically — you do not call `stopped()`
yourself here.

```cpp
void my_target::step() {
    cpu_.step();  // single instruction, returns when done
}
```

For step-over on a CALL instruction, keep executing until the stack pointer
returns to its pre-call value.

### `get_stack()`

Return the current call stack. For single-CPU targets this is almost always a
one-element vector.

```cpp
std::vector<dap::frame_info> my_target::get_stack() const {
    dap::frame_info f;
    f.address     = hex(cpu_.pc());
    f.source_path = resolve_source(cpu_.pc());  // empty if unknown
    f.line        = source_line(cpu_.pc());      // 0 if unknown
    f.name        = symbol_at(cpu_.pc()).value_or(f.address);
    return {f};
}
```

`source_path` is the path to the source file (real or virtual). libdap decides
how to present it to VSCode based on the extension:

- **C/H files on disk**: `sourceReference=0` — VSCode uses the workspace editor
  directly so the cursor stays in the familiar file tab.
- **Assembly files (.s/.asm)**: always assigned a `sourceReference > 0` — VSCode
  opens in debug-virtual editor mode where gutter breakpoints work regardless of
  language mode. Content is served via `get_source()`.
- **Virtual paths** (starting with `/__virtual__/`): assigned a `sourceReference`,
  VSCode fetches content via `get_source()`.

### `get_scopes()`

Return the list of variable groups to show in the Variables panel. libdap
assigns integer IDs to these automatically.

```cpp
std::vector<dap::scope_info> my_target::get_scopes() const {
    return {
        {"Registers", "registers"},
        {"Locals",    "locals"},
    };
}
```

### `get_variables(scope)`

Return the variables for a named scope. The `scope` string matches the `name`
you returned from `get_scopes()`.

```cpp
std::vector<dap::variable_info> my_target::get_variables(
    const std::string &scope) const
{
    if (scope == "Registers") {
        return {
            {"PC", hex(cpu_.pc()), "",  hex(cpu_.pc())},
            {"SP", hex(cpu_.sp()), "",  hex(cpu_.sp())},
            // ...
        };
    }
    return {};
}
```

`variable_info` fields: `name`, `value` (display string), `type` (optional),
`memory_reference` (optional hex address, enables the Memory panel).

### `set_source_breakpoints(path, source_reference, lines)`

Store the requested breakpoints for `path`. Resolve each line number to an
address using your debug info. Return one `breakpoint_info` per line.

```cpp
std::vector<dap::breakpoint_info> my_target::set_source_breakpoints(
    const std::string &path, int source_reference,
    const std::vector<int> &lines)
{
    std::vector<dap::breakpoint_info> result;
    for (int line : lines) {
        auto addr = debug_info_.line_to_address(path, line);
        if (addr) {
            breakpoints_.insert(*addr);
            result.push_back({true, line});
        } else {
            result.push_back({false, line, "No code at this line"});
        }
    }
    return result;
}
```

The `source_reference` parameter is provided so you can identify virtual sources
even when VSCode sends only a reference without the full path. Virtual listing
sources (paths starting with `/__virtual__/`) should be detected by path first;
fall back to `source_reference` if path is ambiguous.

### `get_source(path)`

Return the content of a source file. The default returns `std::nullopt`, which
causes libdap to read the file from disk.

Override this to serve **virtual sources** or to handle path resolution with
source roots:

```cpp
std::optional<dap::source_info> my_target::get_source(
    const std::string &path) const
{
    if (path == "/__virtual__/listing.asm")
        return dap::source_info{build_listing(), "text/x-asm"};

    return std::nullopt;  // let libdap read the real file
}
```

**Gutter breakpoints in assembly files require `mimeType = "text/x-c"`.**
When VSCode opens a source with `mimeType: "text/x-c"`, the C++ extension
activates its breakpoint provider, enabling the gutter regardless of the file's
actual extension. Return `"text/x-c"` for all non-virtual debug-session sources:

```cpp
std::optional<dap::source_info> my_target::get_source(
    const std::string &path) const
{
    if (path.starts_with("/__virtual__/"))
        return dap::source_info{build_listing(), "text/x-asm"};

    // Resolve relative paths through source roots
    auto real = resolve_source_path(path);
    if (!real) return std::nullopt;
    std::ifstream ifs(*real);
    if (!ifs) return std::nullopt;
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return dap::source_info{ss.str(), "text/x-c"};  // always text/x-c
}
```

### `get_breakpoint_locations(path, line, end_line)`

Return the line numbers in `path` that can actually hold a breakpoint, within
the range `[line, end_line]`. Used by VSCode to show or hide gutter indicators.

**This must return results for assembly source files**, or VSCode will suppress
the gutter entirely. Iterate both C source lines and assembly line records from
your debug info:

```cpp
std::vector<dap::bp_location_info> my_target::get_breakpoint_locations(
    const std::string &path, int line, int end_line) const
{
    std::string name = fs::path(path).filename().string();
    std::set<int> valid;

    // C source lines
    for (auto &ln : c_lines_)
        if (ln.line >= line && ln.line <= end_line &&
            fs::path(ln.file).filename() == name)
            valid.insert(ln.line);

    // Assembly source lines (important — without these, .s gutter is disabled)
    for (auto &[line_num, addr] : asm_lines_)
        if (line_num >= line && line_num <= end_line)
            valid.insert(line_num);

    std::vector<dap::bp_location_info> result;
    for (int l : valid) result.push_back({l});
    return result;
}
```

### `evaluate(expr)`

Evaluate a watch expression or hover. Return a display string and an optional
memory address.

```cpp
dap::eval_info my_target::evaluate(const std::string &expr) const {
    if (expr == "PC")
        return {true, hex(cpu_.pc()), "", hex(cpu_.pc())};
    return {false, "", "Unknown: " + expr};
}
```

### Optional methods

| Method | Purpose |
|--------|---------|
| `set_function_breakpoints(names)` | Break on function entry by name |
| `set_instruction_breakpoints(refs)` | Break on raw hex addresses |
| `disassemble(addr, offset, instr_offset, count)` | Populate the Disassembly view |
| `get_loaded_sources()` | Populate the Loaded Sources panel |

---

## Multi-threading

This is the part that requires the most care.

```
┌─────────────────────────────────────────────────────┐
│  main thread                                         │
│                                                      │
│  t.run(in, out)  ←── blocks, reads DAP messages     │
│       │                                              │
│       ├── launch()   → sets up emulator              │
│       ├── resume()   → starts emulation thread ──┐   │
│       ├── pause()    → sets atomic flag          │   │
│       ├── step()     → blocks until done         │   │
│       └── get_stack(), get_variables(), ...      │   │
└──────────────────────────────────────────────────│───┘
                                                   │
┌──────────────────────────────────────────────────▼───┐
│  emulation thread (your code)                        │
│                                                      │
│  while (running_) {                                  │
│      cpu_.step();                                    │
│      if (hit_breakpoint()) {                         │
│          running_ = false;                           │
│          stopped("breakpoint");  // ← safe here      │
│      }                                               │
│  }                                                   │
└─────────────────────────────────────────────────────┘
```

### Rules

**`resume()` must return immediately.** libdap's main loop must stay free to
receive `pause` and other messages while your emulator runs. Start a thread,
return.

**`stopped()`, `output()`, `terminated()`, `send_event_raw()` are thread-safe.**
Call them from the emulation thread at any time. They are no-ops when no session
is active (safe to call before or after `run()`).

**`step()`, `step_in()`, `step_out()` run on the main thread and must block.**
libdap fires the `stopped` event automatically after they return. Do not call
`stopped()` inside these methods.

**`pause()` is called on the main thread while emulation runs on another.**
Use an `std::atomic<bool>` flag; your emulation loop checks it each iteration.

**`get_stack()`, `get_variables()`, `read_memory()` are called after the
emulator has stopped.** libdap only calls these between a `stopped` event and
the next execution command, so no locking is needed for the CPU state itself.
If your emulator can modify state from other threads concurrently, you need your
own mutex.

### Minimal thread-safe skeleton

```cpp
class my_target : public dap::target {
    std::thread          emu_thread_;
    std::atomic<bool>    running_{false};
    std::atomic<bool>    pause_requested_{false};

public:
    void resume() override {
        pause_requested_.store(false);
        running_.store(true);
        if (emu_thread_.joinable()) emu_thread_.join();
        emu_thread_ = std::thread([this] {
            bool hit_bp = false;
            while (running_.load()) {
                cpu_.step();
                if (is_breakpoint(cpu_.pc())) { hit_bp = true; break; }
                if (pause_requested_.load())  {               break; }
            }
            running_.store(false);
            stopped(hit_bp ? "breakpoint" : "pause");
        });
    }

    void pause() override {
        pause_requested_.store(true);
    }

    void disconnect() override {
        running_.store(false);
        if (emu_thread_.joinable()) emu_thread_.join();
    }
};
```

---

## TCP server example

VSCode connects to the debug adapter over TCP. Wrap `run()` in a socket accept
loop:

```cpp
#include <sockpp/tcp_acceptor.h>
#include <dap/target.h>

int main() {
    sockpp::initialize();
    sockpp::tcp_acceptor acc(4711);

    while (true) {
        auto sock = acc.accept();

        socket_stream_buffer buf(sock);
        std::istream in(&buf);
        std::ostream out(&buf);

        my_target t;
        t.run(in, out);  // blocks for the duration of the session
    }
}
```

Each `t.run()` call is one debug session. Construct a fresh target per
connection so there is no state bleed between sessions.

---

## Source path resolution

CDB and MAP files typically embed relative source paths (e.g. `../src/main.c`).
Provide `source_root` and/or `source_roots` in `launch_args` so the library can
resolve them. In `launch.json`:

```json
{
    "sourceRoot": "${workspaceFolder}",
    "sourceRoots": ["${workspaceFolder}/src", "${workspaceFolder}/lib"]
}
```

**Set source roots before loading debug info.** If your implementation calls
the debug-info parser inside `launch()`, configure roots first so that relative
paths in CDB/MAP files are resolved to absolute paths at index-build time. Paths
left unresolved at that point will not match later breakpoint or step lookups.

If `get_source(path)` returns `std::nullopt` and the path cannot be found on
disk after resolution, VSCode shows an "unavailable" placeholder.

---

## Virtual sources

Some content has no real file — a generated disassembly listing, a ROM that
ships without source. Serve it from `get_source()` using a virtual path:

```cpp
// In get_stack(), point the frame at a virtual path:
f.source_path = "/__virtual__/disasm.asm";
f.line        = listing_line_for(cpu_.pc());

// In get_source(), recognise the path and return the content:
std::optional<dap::source_info> my_target::get_source(
    const std::string &path) const
{
    if (path == "/__virtual__/disasm.asm")
        return dap::source_info{generate_listing(), "text/x-asm"};
    return std::nullopt;
}
```

Paths starting with `/__virtual__/` are treated specially by libdap: they are
always assigned a `sourceReference > 0` so VSCode fetches the content from the
adapter, and they are identified as the virtual listing in `set_source_breakpoints`
by path prefix rather than by `sourceReference` value.

---

## SDCC-specific: generated assembly line records

If you use SDCC's CDB format, be aware that SDCC emits `A$<module>$<line>:<addr>`
records for **every** module, including C modules. These records map line numbers
in the **generated intermediate assembly file** (not the C source) to addresses.

If you index these as source line mappings, epilogue addresses will appear to map
to phantom C source lines (e.g. `sieve.c:466` for a file with 167 lines), causing
the step-over loop to halt prematurely in unmapped function epilogues.

**Fix:** only index `A$` (assembly) line records for modules whose source file
has a `.s` or `.asm` extension — genuine assembly modules. Discard `A$` records
from C (`.c`) modules entirely.

---

## Checklist

- [ ] Subclass `dap::target`
- [ ] Set `source_root`/`source_roots` **before** loading CDB/MAP
- [ ] Implement `launch()` — load binary, initialise CPU
- [ ] Implement `resume()` — start emulation on a background thread
- [ ] Implement `pause()` — set an atomic flag
- [ ] Implement `step()` / `step_in()` / `step_out()` — synchronous, no `stopped()` call
- [ ] Implement `get_stack()` — return current frame with source location
- [ ] Implement `get_scopes()` — name your variable groups
- [ ] Implement `get_variables(scope)` — fill in values by group name
- [ ] Implement `set_source_breakpoints(path, source_reference, lines)` — resolve lines → addresses
- [ ] Implement `get_source(path)` — return `mimeType: "text/x-c"` for all non-virtual sources
- [ ] Implement `get_breakpoint_locations(path, line, end_line)` — include assembly lines
- [ ] Implement `evaluate()` — register and symbol lookups at minimum
- [ ] Call `stopped("breakpoint")` / `stopped("pause")` from the emulation thread
- [ ] Call `disconnect()` — join the emulation thread
- [ ] Call `t.run(in, out)` from main
