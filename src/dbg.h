// dbg.h
// Debug state and Z80 emulation for the DAP server.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#pragma once

// Only include headers whose types appear in the public interface or as
// non-pointer private members.  Implementation-only headers (<sstream>,
// <iomanip>, <fstream>, <algorithm>, <format>) belong in the .cpp files.
#include <string>
#include <vector>
#include <array>
#include <deque>
#include <functional>
#include <thread>
#include <filesystem>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <mutex>

#include <nlohmann/json.hpp>
#include <sdcc/cdbg_info.h>
#include <sdcc/segment.h>
#include <sdcc/symbol.h>
#include <z80ex.h>       // Z80EX_CONTEXT, regXX constants, z80ex_get_reg etc.
// z80ex_dasm.h is NOT included here; only disassemble.cpp and source.cpp
// need it and include it directly.
#include <dap/dap.h>

struct source_location {
    std::string file;
    int line;
    bool is_asm = false;
};

struct cpu_snapshot {
    std::array<Z80EX_WORD, 18> regs; // all Z80EX_REG values 0-17
    std::vector<uint8_t>       memory;
};

struct source_content {
    std::string name;
    std::string path;
    std::string content;
    std::string mime_type;
};

// Variable-tree reference IDs used by scopes/variables handlers.
namespace var_ref {
    constexpr int cpu_group     = 100;
    constexpr int cpu_registers = 101;
    constexpr int map_group     = 200;
    constexpr int map_segments  = 201;
    constexpr int map_symbols   = 202;
    constexpr int c_locals      = 300; // current function's local C variables
    constexpr int c_globals     = 400; // all global C variables from CDB
}

class dbg
{
public:
    dbg();
    ~dbg();

    // Register all handler objects with the DAP dispatcher.
    void register_handlers(dap::dap &dispatcher);

    // Event delivery: set by main, called by background threads.
    void set_event_sender(std::function<void(const std::string &)> sender);
    void send_event(const std::string &event_json);
    void queue_event(std::string event_json);

    // Background emulation (for "continue").
    void start_emulation();
    void stop_emulation();
    bool is_emulating() const { return emulation_running_.load(); }

    // Execute one complete Z80 instruction (handles DD/FD/CB/ED prefixes).
    int step_instruction();

    // CPU / memory accessors.
    Z80EX_CONTEXT *cpu() { return cpu_; }
    std::vector<uint8_t> &memory() { return memory_; }
    const std::vector<uint8_t> &memory() const { return memory_; }

    // Breakpoint management.
    std::vector<uint16_t> &instruction_breakpoints() { return instruction_breakpoints_; }
    std::vector<uint16_t> &function_breakpoints()    { return function_breakpoints_; }
    void set_source_breakpoints_for_file(const std::string &file, std::vector<int> lines);
    std::vector<nlohmann::json> resolve_source_breakpoints_for_file(const std::string &file) const;
    void rebuild_source_breakpoint_addresses();

    // Rebuild the merged fast-lookup set after any breakpoint change.
    void rebuild_all_breakpoints();
    // Fast O(1) hit test used in the emulation hot loop.
    bool is_breakpoint(uint16_t pc) const;

    // Pause / disconnect.
    void request_pause()  { pause_requested_.store(true);  }
    void clear_pause()    { pause_requested_.store(false); }
    bool pause_requested() const { return pause_requested_.load(); }

    // Session state.
    int  next_event_seq()         { return event_seq_++; }
    bool launched() const         { return launched_; }
    void set_launched(bool v)     { launched_ = v; }
    bool pending_entry_stop() const    { return pending_entry_stop_; }
    void set_pending_entry_stop(bool v){ pending_entry_stop_ = v; }

    const std::string &virtual_lst_path() const { return virtual_lst_path_; }
    void set_virtual_lst_path(const std::string &p) { virtual_lst_path_ = p; }
    int  virtual_lst_source_reference() const { return virtual_lst_source_reference_; }
    void set_virtual_lst_source_reference(int r) { virtual_lst_source_reference_ = r; }

    // CDB debug info.
    void set_cdb_modules(std::vector<sdcc::cdbg_info_module> m);
    const std::vector<sdcc::cdbg_info_module> &cdb_modules() const { return cdb_modules_; }
    bool has_cdb() const { return !cdb_modules_.empty(); }

    // Source roots for path resolution.
    void set_source_root(const std::string &r) { source_root_ = r; }
    const std::string &source_root() const { return source_root_; }
    void set_source_roots(std::vector<std::string> roots) { source_roots_ = std::move(roots); }
    const std::vector<std::string> &source_roots() const { return source_roots_; }

    // C variable lookup using CDB debug info.
    // Returns the function whose line records span the given address,
    // or nullptr if not found.
    const sdcc::cdbg_info_function *lookup_function_at(uint16_t address) const;
    // Evaluate a C variable and return a displayable "value" string.
    std::string c_variable_value(const sdcc::cdbg_info_symbol &sym) const;

    // MAP info.
    void set_map_symbols(std::vector<sdcc::symbol> symbols);
    const std::vector<sdcc::symbol> &map_symbols() const { return map_symbols_; }
    void set_map_segments(std::vector<sdcc::segment> segments) { map_segments_ = std::move(segments); }
    const std::vector<sdcc::segment> &map_segments() const { return map_segments_; }
    bool has_map() const { return !map_symbols_.empty() || !map_segments_.empty(); }

    // Step-back history (one snapshot per user step press).
    void push_history();
    void pop_history();
    bool can_step_back() const { return !history_.empty(); }

    // Source / symbol lookups (O(1) after index is built).
    std::optional<source_location> lookup_source(uint16_t address) const;     // C only
    std::optional<source_location> lookup_source_any(uint16_t address) const; // C, then asm
    std::optional<uint16_t>        lookup_address(const std::string &file, int line) const;
    std::optional<std::string>     lookup_symbol_exact(uint16_t address) const;
    std::optional<std::string>     lookup_symbol(uint16_t address) const;
    std::optional<uint16_t>        lookup_function_address(const std::string &name) const;
    std::optional<source_location> map_symbol_to_source(const sdcc::symbol &sym) const;
    bool is_in_segment(uint16_t address) const;
    std::optional<std::string>     resolve_source_path(const std::string &path) const;

    // Source file cache (for sourceReference delivery).
    void clear_source_cache();
    int  ensure_source_reference(const std::string &path,
                                 const std::string &mime_type = "text/x-c");
    std::optional<source_content> source_by_reference(int source_reference) const;

    // Full 64 KB disassembly listing — built once at launch, stable for the
    // whole session.  The sourceReference stays fixed; only the highlighted
    // line changes on each step.
    void build_full_listing();                         // call after binary is loaded
    void clear_full_listing();                         // call at launch before reload
    bool has_full_listing() const { return full_listing_built_; }
    const std::string &full_listing_content() const { return full_listing_content_; }
    const std::vector<uint16_t> &full_listing_addrs() const { return full_listing_addrs_; }
    int  full_listing_line_for_addr(uint16_t addr) const; // O(1) via hash map

    // Assembly (address-based) breakpoints set via the virtual listing gutter.
    void set_asm_breakpoints(std::vector<uint16_t> addrs);
    const std::unordered_set<uint16_t> &asm_breakpoints() const { return asm_breakpoints_; }

    // Disassembler callback.
    static uint8_t dasm_readbyte_cb(Z80EX_WORD addr, void *user_data);

    // Formatting.
    static std::string format_hex(uint16_t value, int width);

private:
    // Z80 CPU and memory.
    Z80EX_CONTEXT      *cpu_;
    std::vector<uint8_t> memory_;

    // Breakpoints.
    std::vector<uint16_t> instruction_breakpoints_;
    std::vector<uint16_t> function_breakpoints_;
    std::unordered_map<std::string, std::vector<int>> source_breakpoints_by_file_;
    std::vector<uint16_t> source_breakpoint_addresses_;
    // Merged fast-lookup set (all three lists combined).
    std::unordered_set<uint16_t> all_breakpoints_;

    // Session flags.
    std::atomic<bool> pause_requested_{false};
    int  event_seq_;
    bool launched_;
    bool pending_entry_stop_ = false;

    // Event delivery.
    std::function<void(const std::string &)> send_event_;

    // Background emulation thread.
    std::thread emulation_thread_;
    std::atomic<bool> emulation_running_{false};

    // Virtual listing.
    std::string virtual_lst_path_    = "/__virtual__/listing.lst";
    int virtual_lst_source_reference_ = 1;

    // CDB / MAP debug info.
    std::vector<sdcc::cdbg_info_module> cdb_modules_;
    std::string source_root_;
    std::vector<std::string> source_roots_;
    std::vector<sdcc::symbol>  map_symbols_;
    std::vector<sdcc::segment> map_segments_;

    // Step-back history.
    std::deque<cpu_snapshot> history_;
    static constexpr size_t kMaxHistory = 1000;

    // Address-indexed source lookup (built by rebuild_source_index).
    std::unordered_map<uint16_t, source_location> source_by_addr_;     // C lines only
    std::unordered_map<uint16_t, source_location> asm_by_addr_;        // asm lines only
    // File+line → address index.
    // key = "<filename_stem>:<line>"
    std::unordered_map<std::string, uint16_t> addr_by_file_line_;

    void rebuild_source_index();
    std::string symbolize_asm_line(const std::string &mnemonic) const;

    // Source file content cache.
    std::unordered_map<int, source_content>  source_ref_to_content_;
    std::unordered_map<std::string, int>     source_path_to_ref_;
    int next_source_reference_ = 1000;
    // Full 64 KB listing (built once per launch).
    bool                                   full_listing_built_ = false;
    std::string                            full_listing_content_;
    std::vector<uint16_t>                  full_listing_addrs_;      // line_addrs[i] = address of line i+1
    std::unordered_map<uint16_t, int>      full_listing_addr_to_line_; // address → 1-based line

    std::unordered_set<uint16_t>           asm_breakpoints_;
};
