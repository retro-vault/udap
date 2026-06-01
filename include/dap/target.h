// target.h
// Abstract target interface for the DAP server.
// Implement this to add a new debug target.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <iosfwd>
#include <functional>
#include <atomic>

namespace dap {

// What the client sends at launch time.
struct launch_args {
    std::string program;
    std::string cdb_file;
    std::string map_file;
    std::string source_root;
    std::vector<std::string> source_roots;
    std::optional<uint16_t> start_address;
};

struct frame_info {
    std::string name;
    std::string address;      // hex
    std::string source_path;  // real or virtual path; empty = no source
    int line = 0;
};

struct scope_info {
    std::string name;
    std::string hint;         // "registers" or "locals"
};

struct variable_info {
    std::string name;
    std::string value;
    std::string type;
    std::optional<std::string> memory_reference;
};

struct source_info {
    std::string content;
    std::string mime_type = "text/plain";
};

struct breakpoint_info {
    bool verified = false;
    int line = 0;
    std::string message;
    std::optional<std::string> instruction_reference;
};

struct disasm_info {
    std::string address;
    std::string instruction_bytes;
    std::string instruction;
    std::optional<std::string> symbol;
    std::optional<std::string> source_path;
    std::optional<int> source_line;
};

struct eval_info {
    bool success = true;
    std::string result;
    std::string error_message;
    std::optional<std::string> memory_reference;
};

struct bp_location_info   { int line; };
struct loaded_source_info { std::string name, path; };

// ---------------------------------------------------------------------------
// The one class to implement.
// ---------------------------------------------------------------------------

class target {
public:
    virtual ~target() = default;

    // Lifecycle
    virtual bool launch(const launch_args &args) = 0;
    virtual void disconnect() {}

    // Execution — step/step_in/step_out/step_back block until done;
    // resume() is async: return immediately and call stopped() when done.
    virtual void resume()    = 0;
    virtual void pause()     = 0;
    virtual void step()      = 0;
    virtual void step_in()   = 0;
    virtual void step_out()  = 0;
    virtual void step_back() {}

    // CPU / memory state
    virtual std::vector<uint8_t>             read_memory(uint16_t addr, int count)              const = 0;
    virtual std::vector<frame_info>          get_stack()                                        const = 0;
    virtual std::vector<scope_info>          get_scopes()                                       const = 0;
    virtual std::vector<variable_info>       get_variables(const std::string &scope)            const = 0;

    // Breakpoints
    virtual std::vector<breakpoint_info>     set_source_breakpoints(
        const std::string &path, const std::vector<int> &lines)                                       = 0;
    virtual std::vector<breakpoint_info>     set_function_breakpoints(
        const std::vector<std::string> &names)                                    { return {}; }
    virtual std::vector<breakpoint_info>     set_instruction_breakpoints(
        const std::vector<std::string> &instruction_references)                   { return {}; }

    // Source — return nullopt to let the server read the file from disk.
    virtual std::optional<source_info>       get_source(const std::string &path)                const { return {}; }

    // Optional capabilities
    virtual std::vector<disasm_info>         disassemble(
        int memory_reference, int offset,
        int instruction_offset, int instruction_count)                            const { return {}; }
    virtual eval_info                        evaluate(const std::string &expr)                  const = 0;
    virtual std::vector<bp_location_info>    get_breakpoint_locations(
        const std::string &path, int line, int end_line)                          const { return {}; }
    virtual std::vector<loaded_source_info>  get_loaded_sources()                               const { return {}; }

    // Run the DAP session on the given streams (blocks until client disconnects).
    void run(std::istream &in, std::ostream &out);

protected:
    // Thread-safe event senders — safe to call from any thread (e.g. emulation loop).
    void stopped(const std::string &reason, int thread_id = 1);
    void output(const std::string &text, const std::string &category = "stdout");
    void terminated();
    void send_event_raw(const std::string &event_json);

private:
    std::function<void(const std::string &)> event_sender_;
    std::atomic<int>                         event_seq_{1};
};

} // namespace dap
