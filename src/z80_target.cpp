// z80_target.cpp
// Z80/SDCC debug target implementation.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <iomanip>
#include <set>
#include <algorithm>
#include <cctype>
#include <ranges>

#include <z80ex.h>
#include <z80ex_dasm.h>
#include <sdcc/cdb_parser.h>
#include <sdcc/map_parser.h>
#include <z80_target.h>

namespace fs = std::filesystem;

z80_target::z80_target()
{
    // Wire dbg's background emulation thread to target::send_event_raw().
    // send_event_raw() is a no-op when no session is active, so this is
    // safe to set once at construction.
    dbg_.set_event_sender([this](const std::string &ev) { send_event_raw(ev); });
}

// ---------------------------------------------------------------------------
// Launch
// ---------------------------------------------------------------------------

namespace {

struct ihx_load_result {
    uint16_t entry = 0;
    bool explicit_start = false;
};

ihx_load_result load_ihx(std::istream &in, std::vector<uint8_t> &mem)
{
    uint32_t upper_base = 0;
    uint32_t lowest_data_addr = 0xFFFFFFFF;
    std::optional<uint32_t> explicit_entry;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] != ':' || line.size() < 11) continue;

        auto hex2 = [&](size_t pos) -> uint8_t {
            return static_cast<uint8_t>(std::stoul(line.substr(pos, 2), nullptr, 16));
        };
        auto hex4 = [&](size_t pos) -> uint16_t {
            return static_cast<uint16_t>((hex2(pos) << 8) | hex2(pos + 2));
        };

        uint8_t byte_count = 0;
        uint16_t address = 0;
        uint8_t rec_type = 0;
        try {
            byte_count = hex2(1); address = hex4(3); rec_type = hex2(7);
        } catch (...) { continue; }

        if (line.size() < (11 + static_cast<size_t>(byte_count) * 2)) continue;
        if (rec_type == 0x01) break;

        if (rec_type == 0x00) {
            uint32_t base = upper_base + address;
            if (byte_count > 0 && base < lowest_data_addr) lowest_data_addr = base;
            for (uint8_t i = 0; i < byte_count; ++i) {
                uint8_t byte = 0;
                try { byte = hex2(9 + static_cast<size_t>(i) * 2); } catch (...) { break; }
                size_t dest = base + i;
                if (dest < mem.size()) mem[dest] = byte;
            }
        } else if (rec_type == 0x02 && byte_count >= 2) {
            try { upper_base = static_cast<uint32_t>(hex4(9)) << 4; } catch (...) {}
        } else if (rec_type == 0x04 && byte_count >= 2) {
            try { upper_base = static_cast<uint32_t>(hex4(9)) << 16; } catch (...) {}
        } else if (rec_type == 0x03 && byte_count >= 4) {
            try { explicit_entry = (static_cast<uint32_t>(hex4(9)) << 4) + hex4(13); }
            catch (...) {}
        } else if (rec_type == 0x05 && byte_count >= 4) {
            try {
                explicit_entry = (static_cast<uint32_t>(hex2(9))  << 24) |
                                 (static_cast<uint32_t>(hex2(11)) << 16) |
                                 (static_cast<uint32_t>(hex2(13)) <<  8) |
                                  static_cast<uint32_t>(hex2(15));
            } catch (...) {}
        }
    }

    ihx_load_result result;
    if (explicit_entry) {
        result.entry = static_cast<uint16_t>(*explicit_entry & 0xFFFF);
        result.explicit_start = true;
    } else if (lowest_data_addr != 0xFFFFFFFF) {
        result.entry = static_cast<uint16_t>(lowest_data_addr & 0xFFFF);
    }
    return result;
}

} // anonymous namespace

bool z80_target::launch(const dap::launch_args &args)
{
    z80ex_reset(dbg_.cpu());
    std::fill(dbg_.memory().begin(), dbg_.memory().end(), 0);
    dbg_.set_virtual_lst_source_reference(1);
    dbg_.clear_source_cache();
    dbg_.set_cdb_modules({});
    dbg_.set_source_roots({});
    dbg_.set_map_symbols({});
    dbg_.set_map_segments({});

    uint16_t entry = args.start_address.value_or(0x0000);
    std::string entry_reason = args.start_address ? "from launch startAddress" : "default 0x0000";

    if (!args.program.empty()) {
        std::string bin_path = args.program;
        std::string ext;
        try { ext = fs::path(bin_path).extension().string(); } catch (...) {}

        std::ifstream bin_file(bin_path,
            (ext == ".ihx" || ext == ".hex") ? std::ios::in : std::ios::binary);
        if (bin_file) {
            if (ext == ".ihx" || ext == ".hex") {
                auto res = load_ihx(bin_file, dbg_.memory());
                if (!args.start_address) {
                    entry = res.entry;
                    entry_reason = res.explicit_start
                        ? "from IHX start address record"
                        : "from IHX lowest data address";
                }
            } else {
                bin_file.read(reinterpret_cast<char *>(dbg_.memory().data()),
                              static_cast<std::streamsize>(dbg_.memory().size()));
            }
            std::cerr << "[launch] Loaded: " << bin_path << "\n";
        } else {
            std::cerr << "[launch] ERROR: Cannot open: " << bin_path << "\n";
        }

        fs::path cdb_path = args.cdb_file.empty()
            ? fs::path(bin_path).replace_extension(".cdb")
            : fs::path(args.cdb_file);
        if (fs::exists(cdb_path)) {
            sdcc::cdb_parser parser;
            auto modules = parser.parse(cdb_path.string());
            if (modules) {
                std::cerr << "[launch] Loaded CDB: " << cdb_path.string()
                          << " (" << modules->size() << " modules)\n";
                dbg_.set_cdb_modules(std::move(*modules));
            }
        }

        fs::path map_path = args.map_file.empty()
            ? fs::path(bin_path).replace_extension(".map")
            : fs::path(args.map_file);
        if (fs::exists(map_path)) {
            sdcc::map_parser parser;
            auto map = parser.parse(map_path.string());
            if (map) {
                dbg_.set_map_symbols(map->symbols);
                dbg_.set_map_segments(map->segments);
                std::cerr << "[launch] Loaded MAP: " << map_path.string() << "\n";
            }
        }

        std::string root = args.source_root.empty()
            ? fs::path(bin_path).parent_path().string()
            : args.source_root;
        dbg_.set_source_root(root);
        dbg_.set_source_roots(args.source_roots);
        dbg_.rebuild_source_breakpoint_addresses();

        std::string base;
        try { base = fs::path(bin_path).stem().string(); } catch (...) { base = "listing"; }
        dbg_.set_virtual_lst_path("/__virtual__/" + base + ".asm");
        dbg_.build_full_listing();
    } else {
        dbg_.set_virtual_lst_path("/__virtual__/listing.asm");
    }

    z80ex_set_reg(dbg_.cpu(), regPC, entry);
    z80ex_set_reg(dbg_.cpu(), regSP, 0xFFFF);
    std::cerr << "[launch] Entry: 0x" << std::hex << entry
              << " (" << entry_reason << ")\n";

    dbg_.set_launched(true);
    return true;
}

void z80_target::disconnect()
{
    dbg_.set_launched(false);
    dbg_.stop_emulation();
}

// ---------------------------------------------------------------------------
// Execution
// ---------------------------------------------------------------------------

void z80_target::resume()
{
    dbg_.clear_pause();
    dbg_.start_emulation();
}

void z80_target::pause()
{
    dbg_.request_pause();
}

void z80_target::step()
{
    dbg_.push_history();
    uint16_t start_pc  = z80ex_get_reg(dbg_.cpu(), regPC);
    auto     start_loc = dbg_.lookup_source_any(start_pc);

    auto is_call = [](uint8_t op) {
        return op == 0xCD || (op & 0xC7) == 0xC4 || (op & 0xC7) == 0xC7;
    };
    auto is_ret = [](uint8_t op, uint8_t op2) {
        return op == 0xC9 || (op & 0xC7) == 0xC0
            || (op == 0xED && (op2 == 0x4D || op2 == 0x45));
    };

    auto step_over_loop = [&](auto should_stop) {
        int call_depth = 0;
        int16_t start_sp = static_cast<int16_t>(z80ex_get_reg(dbg_.cpu(), regSP));
        for (int i = 0; i < 500000; ++i) {
            uint16_t pc = z80ex_get_reg(dbg_.cpu(), regPC);
            uint8_t op  = dbg_.memory()[pc];
            uint8_t op2 = (pc + 1 < dbg_.memory().size()) ? dbg_.memory()[pc + 1] : 0;
            int16_t sp_before = static_cast<int16_t>(z80ex_get_reg(dbg_.cpu(), regSP));
            dbg_.step_instruction();
            int16_t sp_after = static_cast<int16_t>(z80ex_get_reg(dbg_.cpu(), regSP));
            int16_t sp_delta = sp_after - sp_before;
            if (sp_delta == -2 && is_call(op))  call_depth++;
            else if (sp_delta == 2 && is_ret(op, op2) && call_depth > 0) call_depth--;
            if (sp_after >= start_sp) call_depth = 0;
            if (call_depth > 0) continue;
            if (should_stop(z80ex_get_reg(dbg_.cpu(), regPC))) break;
        }
    };

    if (start_loc && start_loc->is_asm) {
        step_over_loop([&](uint16_t pc) {
            auto loc = dbg_.lookup_source_any(pc);
            return loc && (loc->file != start_loc->file || loc->line != start_loc->line);
        });
    } else if (start_loc) {
        step_over_loop([&](uint16_t pc) {
            auto loc = dbg_.lookup_source(pc);
            return loc && (loc->file != start_loc->file || loc->line != start_loc->line);
        });
    } else {
        dbg_.step_instruction();
    }
}

void z80_target::step_in()
{
    dbg_.push_history();
    uint16_t start_pc  = z80ex_get_reg(dbg_.cpu(), regPC);
    auto     start_loc = dbg_.lookup_source_any(start_pc);

    if (start_loc && start_loc->is_asm) {
        for (int i = 0; i < 10000; ++i) {
            dbg_.step_instruction();
            uint16_t pc = z80ex_get_reg(dbg_.cpu(), regPC);
            auto loc = dbg_.lookup_source_any(pc);
            if (loc && (loc->file != start_loc->file || loc->line != start_loc->line)) break;
        }
    } else if (start_loc && dbg_.has_cdb()) {
        for (int i = 0; i < 10000; ++i) {
            uint16_t pc        = z80ex_get_reg(dbg_.cpu(), regPC);
            uint8_t  op        = dbg_.memory()[pc];
            int16_t  sp_before = static_cast<int16_t>(z80ex_get_reg(dbg_.cpu(), regSP));
            dbg_.step_instruction();
            int16_t  sp_after = static_cast<int16_t>(z80ex_get_reg(dbg_.cpu(), regSP));
            uint16_t new_pc   = z80ex_get_reg(dbg_.cpu(), regPC);
            auto loc = dbg_.lookup_source(new_pc);
            if (loc && (loc->file != start_loc->file || loc->line != start_loc->line)) break;
            if (!loc) {
                bool entered_call = (sp_after - sp_before == -2)
                    && (op == 0xCD || (op & 0xC7) == 0xC4 || (op & 0xC7) == 0xC7);
                if (entered_call && dbg_.lookup_source_any(new_pc)) break;
            }
        }
    } else {
        dbg_.step_instruction();
    }
}

void z80_target::step_out()
{
    dbg_.push_history();
    int16_t entry_sp = static_cast<int16_t>(z80ex_get_reg(dbg_.cpu(), regSP));
    for (int i = 0; i < 500000; ++i) {
        dbg_.step_instruction();
        if (static_cast<int16_t>(z80ex_get_reg(dbg_.cpu(), regSP)) > entry_sp) break;
    }
}

void z80_target::step_back()
{
    dbg_.pop_history();
}

// ---------------------------------------------------------------------------
// State queries
// ---------------------------------------------------------------------------

std::vector<uint8_t> z80_target::read_memory(uint16_t addr, int count) const
{
    const auto &mem = dbg_.memory();
    uint32_t start = addr;
    uint32_t avail = static_cast<uint32_t>(mem.size()) - start;
    uint32_t n     = std::min(static_cast<uint32_t>(std::max(0, count)), avail);
    return {mem.begin() + start, mem.begin() + start + n};
}

std::vector<dap::frame_info> z80_target::get_stack() const
{
    uint16_t pc  = z80ex_get_reg(dbg_.cpu(), regPC);
    dap::frame_info f;
    f.address = dbg::format_hex(pc, 4);

    auto src = dbg_.has_cdb() ? dbg_.lookup_source_any(pc) : std::nullopt;
    if (src) {
        f.source_path = src->file;
        f.name        = fs::path(src->file).filename().string()
                        + ":" + std::to_string(src->line);
        f.line        = src->line;
    } else {
        f.source_path = dbg_.virtual_lst_path();
        f.name        = dbg_.lookup_symbol(pc).value_or(dbg::format_hex(pc, 4));
        f.line        = dbg_.full_listing_line_for_addr(pc);
    }
    return {f};
}

// Scopes — flat: no nesting, no integer variable references in the target.
std::vector<dap::scope_info> z80_target::get_scopes() const
{
    std::vector<dap::scope_info> scopes;
    scopes.push_back({"Registers", "registers"});

    if (dbg_.has_cdb()) {
        uint16_t pc = z80ex_get_reg(dbg_.cpu(), regPC);
        auto *fn = dbg_.lookup_function_at(pc);
        if (fn && !fn->local_symbols.empty())
            scopes.push_back({"Locals", "locals"});
        scopes.push_back({"Globals", "locals"});
    }

    if (dbg_.has_map()) {
        scopes.push_back({"MAP Segments", "locals"});
        scopes.push_back({"MAP Symbols",  "locals"});
    }

    return scopes;
}

// Variables — dispatched by scope name.
std::vector<dap::variable_info> z80_target::get_variables(const std::string &scope) const
{
    std::vector<dap::variable_info> vars;
    auto *c = dbg_.cpu();

    auto make_reg = [](const char *name, uint16_t val, int width, bool with_mem) {
        dap::variable_info v;
        v.name  = name;
        v.value = dbg::format_hex(val, width);
        if (with_mem) v.memory_reference = dbg::format_hex(val, 4);
        return v;
    };

    if (scope == "Registers") {
        vars.push_back(make_reg("AF", z80ex_get_reg(c, regAF), 4, false));
        vars.push_back(make_reg("BC", z80ex_get_reg(c, regBC), 4, true));
        vars.push_back(make_reg("DE", z80ex_get_reg(c, regDE), 4, true));
        vars.push_back(make_reg("HL", z80ex_get_reg(c, regHL), 4, true));
        vars.push_back(make_reg("IX", z80ex_get_reg(c, regIX), 4, true));
        vars.push_back(make_reg("IY", z80ex_get_reg(c, regIY), 4, true));
        vars.push_back(make_reg("SP", z80ex_get_reg(c, regSP), 4, true));
        vars.push_back(make_reg("PC", z80ex_get_reg(c, regPC), 4, true));
        vars.push_back(make_reg("R",  z80ex_get_reg(c, regR),  2, false));
        vars.push_back(make_reg("I",  z80ex_get_reg(c, regI),  2, false));
        vars.push_back(make_reg("F",
            static_cast<uint16_t>(z80ex_get_reg(c, regAF) & 0xFF), 2, false));

    } else if (scope == "Locals") {
        uint16_t pc = z80ex_get_reg(dbg_.cpu(), regPC);
        auto *fn = dbg_.lookup_function_at(pc);
        if (fn)
            for (const auto &sym : fn->local_symbols)
                vars.push_back({sym.name, dbg_.c_variable_value(sym), sym.type_info});

    } else if (scope == "Globals") {
        for (const auto &mod : dbg_.cdb_modules())
            for (const auto &sym : mod.global_symbols) {
                if (sym.type_info.find("DF") != std::string::npos) continue;
                vars.push_back({sym.name, dbg_.c_variable_value(sym), sym.type_info});
            }

    } else if (scope == "MAP Segments") {
        for (const auto &seg : dbg_.map_segments()) {
            uint16_t addr = static_cast<uint16_t>(seg.address & 0xFFFF);
            vars.push_back({seg.name,
                "addr=" + dbg::format_hex(addr, 4)
                + ", size=" + dbg::format_hex(static_cast<uint16_t>(seg.size & 0xFFFF), 4)
                + ", " + seg.attributes,
                "", dbg::format_hex(addr, 4)});
        }

    } else if (scope == "MAP Symbols") {
        for (const auto &sym : dbg_.map_symbols()) {
            uint16_t addr = static_cast<uint16_t>(sym.address & 0xFFFF);
            vars.push_back({sym.name, dbg::format_hex(addr, 4), "", dbg::format_hex(addr, 4)});
        }
    }

    return vars;
}

// ---------------------------------------------------------------------------
// Breakpoints
// ---------------------------------------------------------------------------

std::vector<dap::breakpoint_info> z80_target::set_source_breakpoints(
    const std::string &path, const std::vector<int> &lines)
{
    if (path == dbg_.virtual_lst_path()) {
        std::vector<dap::breakpoint_info> result;
        std::vector<uint16_t> addrs;
        const auto &line_addrs = dbg_.full_listing_addrs();
        for (int line : lines) {
            int idx = line - 1;
            if (idx >= 0 && static_cast<size_t>(idx) < line_addrs.size()) {
                uint16_t addr = line_addrs[static_cast<size_t>(idx)];
                addrs.push_back(addr);
                result.push_back({true, line, "", dbg::format_hex(addr, 4)});
            } else {
                result.push_back({false, line, "Line out of range", std::nullopt});
            }
        }
        dbg_.set_asm_breakpoints(std::move(addrs));
        return result;
    }

    dbg_.set_source_breakpoints_for_file(path, lines);
    auto raw = dbg_.resolve_source_breakpoints_for_file(path);
    dbg_.rebuild_source_breakpoint_addresses();

    std::vector<dap::breakpoint_info> result;
    for (const auto &bp : raw)
        result.push_back({bp.value("verified", false), bp.value("line", 0),
                          bp.value("message", ""), std::nullopt});
    return result;
}

std::vector<dap::breakpoint_info> z80_target::set_function_breakpoints(
    const std::vector<std::string> &names)
{
    dbg_.function_breakpoints().clear();
    std::vector<dap::breakpoint_info> result;
    for (const auto &name : names) {
        auto addr = dbg_.lookup_function_address(name);
        if (addr) {
            dbg_.function_breakpoints().push_back(*addr);
            result.push_back({true, 0, "", dbg::format_hex(*addr, 4)});
        } else {
            result.push_back({false, 0, "Function not found: " + name, std::nullopt});
        }
    }
    dbg_.rebuild_all_breakpoints();
    return result;
}

std::vector<dap::breakpoint_info> z80_target::set_instruction_breakpoints(
    const std::vector<std::string> &instruction_references)
{
    dbg_.instruction_breakpoints().clear();
    for (const auto &ref : instruction_references) {
        try {
            dbg_.instruction_breakpoints().push_back(
                static_cast<uint16_t>(std::stoul(ref, nullptr, 0)));
        } catch (...) {}
    }
    dbg_.rebuild_all_breakpoints();

    std::vector<dap::breakpoint_info> result;
    for (uint16_t addr : dbg_.instruction_breakpoints())
        result.push_back({true, 0, "", dbg::format_hex(addr, 4)});
    return result;
}

// ---------------------------------------------------------------------------
// Source content
// ---------------------------------------------------------------------------

std::optional<dap::source_info> z80_target::get_source(const std::string &path) const
{
    if (path == dbg_.virtual_lst_path()) {
        if (!dbg_.has_full_listing())
            dbg_.build_full_listing();
        return dap::source_info{dbg_.full_listing_content(), "text/x-asm"};
    }

    // Resolve the path through source roots (handles relative CDB paths).
    auto resolved = dbg_.resolve_source_path(path);
    std::string real_path = resolved ? *resolved : path;

    std::ifstream ifs(real_path);
    if (!ifs) return std::nullopt;
    std::ostringstream ss;
    ss << ifs.rdbuf();

    std::string mime = "text/plain";
    try {
        auto ext = fs::path(real_path).extension().string();
        if (ext == ".c" || ext == ".h")                         mime = "text/x-c";
        else if (ext == ".s" || ext == ".asm" || ext == ".lst") mime = "text/x-asm";
    } catch (...) {}

    return dap::source_info{ss.str(), mime};
}

// ---------------------------------------------------------------------------
// Disassembly
// ---------------------------------------------------------------------------

std::vector<dap::disasm_info> z80_target::disassemble(
    int memory_reference, int offset,
    int instruction_offset, int instruction_count) const
{
    uint16_t base_addr =
        static_cast<uint16_t>((memory_reference + offset) & 0xFFFF);
    // z80ex_dasm takes non-const void*; safe: callback only reads
    auto &mem = const_cast<std::vector<uint8_t> &>(dbg_.memory());
    uint16_t addr = base_addr;

    if (instruction_offset < 0) {
        int backstep = -instruction_offset;
        int rewind   = std::min(static_cast<int>(addr), backstep * 4);
        uint16_t scan = addr - rewind;
        std::vector<uint16_t> addrs;
        uint16_t pos = scan;
        while (pos <= addr + 3 && pos < mem.size()) {
            addrs.push_back(pos);
            char buf[64]; int t1 = 0, t2 = 0;
            int len = z80ex_dasm(buf, sizeof(buf), 0, &t1, &t2,
                                 dbg::dasm_readbyte_cb, pos, &mem);
            pos += static_cast<uint16_t>((len > 0) ? len : 1);
        }
        int target_idx = static_cast<int>(addrs.size()) - 1;
        for (int i = 0; i < static_cast<int>(addrs.size()); ++i)
            if (addrs[i] >= addr) { target_idx = i; break; }
        addr = addrs[std::max(0, target_idx - backstep)];
    } else if (instruction_offset > 0) {
        for (int i = 0; i < instruction_offset && addr < mem.size(); ++i) {
            char buf[64]; int t1 = 0, t2 = 0;
            int len = z80ex_dasm(buf, sizeof(buf), 0, &t1, &t2,
                                 dbg::dasm_readbyte_cb, addr, &mem);
            addr += static_cast<uint16_t>((len > 0) ? len : 1);
        }
    }

    std::vector<dap::disasm_info> result;
    for (int i = 0; i < instruction_count && addr < mem.size(); ++i) {
        char buf[64]; int ts1 = 0, ts2 = 0;
        int ilen = z80ex_dasm(buf, sizeof(buf), 0, &ts1, &ts2,
                              dbg::dasm_readbyte_cb, addr, &mem);
        if (ilen <= 0) ilen = 1;

        std::ostringstream bytes_oss;
        for (int j = 0; j < ilen && (addr + j) < mem.size(); ++j) {
            if (j > 0) bytes_oss << " ";
            bytes_oss << std::uppercase << std::setw(2) << std::setfill('0')
                      << std::hex << static_cast<int>(mem[addr + j]);
        }

        dap::disasm_info info;
        info.address           = dbg::format_hex(addr, 4);
        info.instruction_bytes = bytes_oss.str();
        info.instruction       = buf;

        auto sym = dbg_.lookup_symbol_exact(addr);
        if (sym) info.symbol = *sym;

        auto loc = dbg_.lookup_source(addr);
        if (loc) { info.source_path = loc->file; info.source_line = loc->line; }

        result.push_back(info);
        addr += static_cast<uint16_t>(ilen);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Evaluate
// ---------------------------------------------------------------------------

dap::eval_info z80_target::evaluate(const std::string &expr) const
{
    std::string upper = expr;
    std::ranges::transform(upper, upper.begin(), ::toupper);

    struct reg_entry { const char *name; Z80_REG_T id; int width; };
    static constexpr reg_entry regs[] = {
        {"AF", regAF, 4}, {"BC", regBC, 4}, {"DE", regDE, 4},
        {"HL", regHL, 4}, {"IX", regIX, 4}, {"IY", regIY, 4},
        {"SP", regSP, 4}, {"PC", regPC, 4},
        {"A",  regAF, 2}, {"F",  regAF, 2},
        {"B",  regBC, 2}, {"C",  regBC, 2},
        {"D",  regDE, 2}, {"E",  regDE, 2},
        {"H",  regHL, 2}, {"L",  regHL, 2},
        {"R",  regR,  2}, {"I",  regI,  2},
    };

    for (const auto &reg : regs) {
        if (upper != reg.name) continue;
        uint16_t val = z80ex_get_reg(dbg_.cpu(), reg.id);
        if (reg.width == 2 && std::strlen(reg.name) == 1) {
            char ch = reg.name[0];
            val = (ch == 'A' || ch == 'B' || ch == 'D' || ch == 'H')
                ? (val >> 8) & 0xFF : val & 0xFF;
        }
        dap::eval_info r;
        r.result = dbg::format_hex(val, reg.width);
        if (reg.width == 4) r.memory_reference = dbg::format_hex(val, 4);
        return r;
    }

    for (const auto &sym : dbg_.map_symbols()) {
        if (sym.name == expr || sym.name == "_" + expr) {
            uint16_t addr = static_cast<uint16_t>(sym.address & 0xFFFF);
            return {true, dbg::format_hex(addr, 4), "", dbg::format_hex(addr, 4)};
        }
    }

    try {
        uint16_t val = 0;
        if (expr.size() > 2 && expr[0] == '0' && (expr[1] == 'x' || expr[1] == 'X'))
            val = static_cast<uint16_t>(std::stoul(expr, nullptr, 0));
        else if (expr.size() > 1 && expr[0] == '$')
            val = static_cast<uint16_t>(std::stoul(expr.substr(1), nullptr, 16));
        else
            return {false, "", "Cannot evaluate: " + expr};
        uint8_t byte = dbg_.memory()[val];
        return {true, dbg::format_hex(byte, 2), "", dbg::format_hex(val, 4)};
    } catch (...) {}

    return {false, "", "Cannot evaluate: " + expr};
}

// ---------------------------------------------------------------------------
// Breakpoint locations
// ---------------------------------------------------------------------------

std::vector<dap::bp_location_info> z80_target::get_breakpoint_locations(
    const std::string &path, int line, int end_line) const
{
    std::string query_name = fs::path(path).filename().string();
    std::set<int> valid_lines;

    for (const auto &mod : dbg_.cdb_modules())
        for (const auto &ln : mod.lines)
            if (ln.line >= line && ln.line <= end_line &&
                fs::path(ln.file).filename().string() == query_name)
                valid_lines.insert(ln.line);

    for (const auto &sym : dbg_.map_symbols()) {
        auto loc = dbg_.map_symbol_to_source(sym);
        if (!loc || loc->line < line || loc->line > end_line) continue;
        if (fs::path(loc->file).filename().string() == query_name)
            valid_lines.insert(loc->line);
    }

    std::vector<dap::bp_location_info> result;
    for (int l : valid_lines) result.push_back({l});
    return result;
}

// ---------------------------------------------------------------------------
// Loaded sources
// ---------------------------------------------------------------------------

std::vector<dap::loaded_source_info> z80_target::get_loaded_sources() const
{
    std::set<std::string> seen;
    std::vector<dap::loaded_source_info> result;

    auto add = [&](const std::string &file) {
        auto resolved = dbg_.resolve_source_path(file);
        std::string path = resolved ? *resolved : file;
        if (!seen.insert(path).second) return;
        result.push_back({fs::path(path).filename().string(), path});
    };

    for (const auto &mod : dbg_.cdb_modules()) {
        if (!mod.file.empty()) add(mod.file.empty() ? mod.name : mod.file);
        for (const auto &ln : mod.lines)
            if (!ln.file.empty()) add(ln.file);
    }
    return result;
}
