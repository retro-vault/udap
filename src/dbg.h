// dbg.h
// Debug Adapter Protocol (DAP) server class for Z80 emulation.
//
// This file defines the `dbg` class which implements a DAP-compliant debugger
// for a Z80 CPU using the z80ex library. It holds all emulation state and
// registers handler classes with the DAP dispatcher.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#pragma once
#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <filesystem>
#include <optional>
#include <unordered_map>
#include <atomic>

#include <nlohmann/json.hpp>
#include <sdcc/cdbg_info.h>
#include <sdcc/segment.h>
#include <sdcc/symbol.h>
#include <z80ex.h>
#include <z80ex_dasm.h>
#include <dap/dap.h>

struct source_location {
    std::string file;
    int line;
};

struct source_content {
    std::string name;
    std::string path;
    std::string content;
    std::string mime_type;
};

class dbg
{
public:
    dbg();
    ~dbg();

    // Register all handler objects with the DAP dispatcher.
    void register_handlers(dap::dap &dispatcher);

    // Event sending (set by main before running the dispatcher).
    void set_event_sender(std::function<void(const std::string &)> sender);
    void send_event(const std::string &event_json);

    // Execute one complete Z80 instruction (handles DD/FD/CB/ED prefixes).
    int step_instruction();

    // Accessors for handler classes.
    Z80EX_CONTEXT *cpu() { return cpu_; }
    std::vector<uint8_t> &memory() { return memory_; }
    const std::vector<uint8_t> &memory() const { return memory_; }
    std::vector<uint16_t> &breakpoints() { return breakpoints_; }
    std::vector<uint16_t> &instruction_breakpoints() { return instruction_breakpoints_; }
    std::vector<uint16_t> &function_breakpoints() { return function_breakpoints_; }
    void request_pause() { pause_requested_.store(true); }
    void clear_pause() { pause_requested_.store(false); }
    bool pause_requested() const { return pause_requested_.load(); }
    int next_event_seq() { return event_seq_++; }
    bool launched() const { return launched_; }
    void set_launched(bool v) { launched_ = v; }
    bool pending_entry_stop() const { return pending_entry_stop_; }
    void set_pending_entry_stop(bool v) { pending_entry_stop_ = v; }
    const std::string &virtual_lst_path() const { return virtual_lst_path_; }
    void set_virtual_lst_path(const std::string &p) { virtual_lst_path_ = p; }
    int virtual_lst_source_reference() const { return virtual_lst_source_reference_; }
    void set_virtual_lst_source_reference(int r) { virtual_lst_source_reference_ = r; }

    // CDB debug info.
    void set_cdb_modules(std::vector<sdcc::cdbg_info_module> m) { cdb_modules_ = std::move(m); }
    const std::vector<sdcc::cdbg_info_module> &cdb_modules() const { return cdb_modules_; }
    bool has_cdb() const { return !cdb_modules_.empty(); }
    void set_source_root(const std::string &r) { source_root_ = r; }
    const std::string &source_root() const { return source_root_; }
    void set_source_roots(std::vector<std::string> roots) { source_roots_ = std::move(roots); }
    const std::vector<std::string> &source_roots() const { return source_roots_; }
    void set_map_symbols(std::vector<sdcc::symbol> symbols) { map_symbols_ = std::move(symbols); }
    const std::vector<sdcc::symbol> &map_symbols() const { return map_symbols_; }
    void set_map_segments(std::vector<sdcc::segment> segments) { map_segments_ = std::move(segments); }
    const std::vector<sdcc::segment> &map_segments() const { return map_segments_; }
    bool has_map() const { return !map_symbols_.empty() || !map_segments_.empty(); }
    std::optional<source_location> lookup_source(uint16_t address) const;
    std::optional<uint16_t> lookup_address(const std::string &file, int line) const;
    std::optional<std::string> lookup_symbol_exact(uint16_t address) const;
    std::optional<std::string> lookup_symbol(uint16_t address) const;
    std::optional<uint16_t> lookup_function_address(const std::string &name) const;
    bool is_in_segment(uint16_t address) const;
    std::optional<std::string> resolve_source_path(const std::string &path) const;
    void set_source_breakpoints_for_file(const std::string &file,
                                         std::vector<int> lines);
    std::vector<nlohmann::json> resolve_source_breakpoints_for_file(
        const std::string &file) const;
    void rebuild_source_breakpoint_addresses();
    void clear_source_cache();
    int ensure_source_reference(const std::string &path,
                                const std::string &mime_type = "text/x-c");
    std::optional<source_content> source_by_reference(int source_reference) const;

    // Disassembler support.
    static uint8_t dasm_readbyte_cb(Z80EX_WORD addr, void *user_data);

    // Formatting helper.
    std::string format_hex(uint16_t value, int width);

private:
    Z80EX_CONTEXT *cpu_;
    std::vector<uint8_t> memory_;
    std::vector<uint16_t> breakpoints_;
    std::vector<uint16_t> instruction_breakpoints_;
    std::vector<uint16_t> function_breakpoints_;
    std::atomic<bool> pause_requested_{false};
    int event_seq_;
    bool launched_;
    bool pending_entry_stop_ = false;
    std::function<void(const std::string &)> send_event_;

    std::string virtual_lst_path_ = "/__virtual__/listing.lst";
    int virtual_lst_source_reference_ = 1;

    std::vector<sdcc::cdbg_info_module> cdb_modules_;
    std::string source_root_;
    std::vector<std::string> source_roots_;
    std::vector<sdcc::symbol> map_symbols_;
    std::vector<sdcc::segment> map_segments_;
    std::unordered_map<std::string, std::vector<int>> source_breakpoints_by_file_;

    std::unordered_map<int, source_content> source_ref_to_content_;
    std::unordered_map<std::string, int> source_path_to_ref_;
    int next_source_reference_ = 1000;
};
