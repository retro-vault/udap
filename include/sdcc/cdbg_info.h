// cdbg_info.h
// Hierarchical structures for SDCC C debug information (.cdb files).
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>

namespace sdcc {

    // Storage classes for C variables as encoded in CDB symbol records.
    // Each value is the raw character from the CDB file.
    enum class storage_class : char {
        unknown      = 0,
        stack        = 'B', // IX-relative (SDCC frame pointer convention)
        reg          = 'R', // CPU register
        internal     = 'I', // static with fixed address (look up via MAP)
        ext          = 'E', // extern with fixed address
        global_auto  = 'A', // global auto (address from MAP)
    };

    struct cdbg_info_symbol {
        std::string name;
        std::string scope;      // "global" or "local"
        std::string type_info;  // type string inside parens, e.g. "{2}SI:S"

        // Storage location — parsed from the fields after the closing ')'.
        storage_class storage = storage_class::unknown;
        int offset = 0;         // IX offset for 'B', address for 'I'/'E'
        int size = 1;           // byte size, from the {N} prefix in type_info
        // Register list for 'R' storage, e.g. "c" (C), "c,b" (BC), "e,d" (DE).
        // Registers are listed low-byte first (SDCC Z80 convention).
        std::string reg_list;
    };

    struct cdbg_info_function {
        std::string name;
        std::string scope;
        std::vector<cdbg_info_symbol> local_symbols;
        // Address range populated from L:F/L:XF/L:G/L:XG CDB line records.
        uint16_t start_addr = 0xFFFF;
        uint16_t end_addr   = 0xFFFF;
        bool has_addr() const { return start_addr != 0xFFFF; }
    };

    struct cdbg_info_type {
        std::string name;
        std::string scope;
        std::string type_info;
    };

    struct cdbg_info_line {
        std::string file;
        int line = 0;
        uint16_t address = 0;
        std::string scope;
    };

    struct cdbg_info_module {
        std::string name;
        std::string file;
        std::vector<cdbg_info_function> functions;
        std::vector<cdbg_info_symbol>   global_symbols;
        std::vector<cdbg_info_type>     types;
        std::vector<cdbg_info_line>     lines;
        // Assembly line → address (from L:A$ records).
        // Populated for both C modules (mixed C+asm) and pure-assembly stub
        // modules (e.g. crt0) that have no M: record in the CDB.
        std::unordered_map<int, uint16_t> asm_lines;
    };

} // namespace sdcc
