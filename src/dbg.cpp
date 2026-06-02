// dbg.cpp
// Debug state implementation for the Z80 DAP server.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <sstream>
#include <iomanip>
#include <fstream>
#include <algorithm>
#include <format>
#include <regex>
#include <deque>
#include <z80ex_dasm.h>
#include <dbg.h>
// ---------------------------------------------------------------------------
// Event delivery
// ---------------------------------------------------------------------------

void dbg::set_event_sender(std::function<void(const std::string &)> sender)
{
    send_event_ = std::move(sender);
}

void dbg::send_event(const std::string &event_json)
{
    if (send_event_)
        send_event_(event_json);
}

void dbg::queue_event(std::string event_json)
{
    // Called from background threads: send directly via the thread-safe sender.
    send_event(event_json);
}

// ---------------------------------------------------------------------------
// Background emulation (for "continue")
// ---------------------------------------------------------------------------

void dbg::start_emulation()
{
    if (emulation_running_.exchange(true))
        return; // already running

    if (emulation_thread_.joinable())
        emulation_thread_.join();

    emulation_thread_ = std::thread([this]() {
        bool hit_breakpoint = false;

        while (emulation_running_.load(std::memory_order_relaxed)) {
            step_instruction();

            if (!launched_)
                break;

            uint16_t pc = z80ex_get_reg(cpu_, regPC);

            if (is_breakpoint(pc)) {
                hit_breakpoint = true;
                break;
            }
            if (pause_requested()) {
                clear_pause();
                break;
            }
            // HALT instruction stops execution.
            if (memory_[pc] == 0x76)
                break;
        }

        emulation_running_.store(false);

        nlohmann::json j;
        j["seq"]   = next_event_seq();
        j["type"]  = "event";
        j["event"] = "stopped";
        j["body"]  = {
            {"reason",          hit_breakpoint ? "breakpoint" : "pause"},
            {"threadId",        1},
            {"allThreadsStopped", true}
        };
        queue_event(j.dump());
    });
}

void dbg::stop_emulation()
{
    emulation_running_.store(false);
    if (emulation_thread_.joinable())
        emulation_thread_.join();
}

// ---------------------------------------------------------------------------
// Breakpoints
// ---------------------------------------------------------------------------

void dbg::set_source_breakpoints_for_file(const std::string &file,
                                          std::vector<int> lines)
{
    if (file.empty()) return;
    if (lines.empty())
        source_breakpoints_by_file_.erase(file);
    else
        source_breakpoints_by_file_[file] = std::move(lines);
}

std::vector<nlohmann::json> dbg::resolve_source_breakpoints_for_file(
    const std::string &file) const
{
    std::vector<nlohmann::json> out;
    auto it = source_breakpoints_by_file_.find(file);
    if (it == source_breakpoints_by_file_.end()) return out;

    for (int line : it->second) {
        auto addr = lookup_address(file, line);
        if (addr) {
            out.push_back({{"verified", true}, {"line", line}});
        } else if (!has_cdb() && !has_map()) {
            out.push_back({{"verified", false}, {"line", line},
                           {"message", "Pending symbol resolution (CDB/MAP not loaded yet)"}});
        } else {
            out.push_back({{"verified", false}, {"line", line},
                           {"message", "No address mapping for this line"}});
        }
    }
    return out;
}

void dbg::rebuild_source_breakpoint_addresses()
{
    source_breakpoint_addresses_.clear();
    std::unordered_set<uint16_t> seen;

    for (const auto &[file, lines] : source_breakpoints_by_file_) {
        for (int line : lines) {
            auto addr = lookup_address(file, line);
            if (addr && seen.insert(*addr).second)
                source_breakpoint_addresses_.push_back(*addr);
        }
    }
    rebuild_all_breakpoints();
}

void dbg::rebuild_all_breakpoints()
{
    all_breakpoints_.clear();
    for (uint16_t a : source_breakpoint_addresses_) all_breakpoints_.insert(a);
    for (uint16_t a : instruction_breakpoints_)     all_breakpoints_.insert(a);
    for (uint16_t a : function_breakpoints_)         all_breakpoints_.insert(a);
    for (uint16_t a : asm_breakpoints_)              all_breakpoints_.insert(a);
}

void dbg::clear_full_listing()
{
    full_listing_built_ = false;
    full_listing_content_.clear();
    full_listing_addrs_.clear();
    full_listing_addr_to_line_.clear();
}

int dbg::full_listing_line_for_addr(uint16_t addr) const
{
    auto it = full_listing_addr_to_line_.find(addr);
    return (it != full_listing_addr_to_line_.end()) ? it->second : 1;
}

void dbg::set_asm_breakpoints(std::vector<uint16_t> addrs)
{
    asm_breakpoints_.clear();
    for (uint16_t a : addrs) asm_breakpoints_.insert(a);
    rebuild_all_breakpoints();
}

bool dbg::is_breakpoint(uint16_t pc) const
{
    return all_breakpoints_.count(pc) > 0;
}

// ---------------------------------------------------------------------------
// CDB / MAP data setters (also rebuild the source index)
// ---------------------------------------------------------------------------

void dbg::set_cdb_modules(std::vector<sdcc::cdbg_info_module> m)
{
    cdb_modules_ = std::move(m);
    rebuild_source_index();
}

void dbg::set_map_symbols(std::vector<sdcc::symbol> symbols)
{
    map_symbols_ = std::move(symbols);
    rebuild_source_index();
}

// ---------------------------------------------------------------------------
// Step-back history
// ---------------------------------------------------------------------------

static const Z80_REG_T kAllRegs[] = {
    regAF, regBC, regDE, regHL, regAF_, regBC_, regDE_, regHL_,
    regIX, regIY, regPC, regSP, regI,   regR,   regR7,  regIM,
    regIFF1, regIFF2
};

void dbg::push_history()
{
    if (history_.size() >= kMaxHistory)
        history_.pop_front();
    cpu_snapshot s;
    for (int i = 0; i < 18; ++i)
        s.regs[static_cast<size_t>(i)] = z80ex_get_reg(cpu_, kAllRegs[i]);
    s.memory = memory_;
    history_.push_back(std::move(s));
}

void dbg::pop_history()
{
    if (history_.empty()) return;
    const auto &s = history_.back();
    memory_ = s.memory;
    for (int i = 0; i < 18; ++i)
        z80ex_set_reg(cpu_, kAllRegs[i], s.regs[static_cast<size_t>(i)]);
    history_.pop_back();
}

// ---------------------------------------------------------------------------
// Source index (built once after CDB/MAP is loaded)
// ---------------------------------------------------------------------------

void dbg::rebuild_source_index()
{
    source_by_addr_.clear();
    asm_by_addr_.clear();
    addr_by_file_line_.clear();

    for (const auto &mod : cdb_modules_) {
        // Assembly lines: only index lines for genuine assembly modules (.s/.asm).
        // C modules also have asm_lines, but those are line numbers from the
        // *generated* intermediate .asm file — not C source lines. Using them
        // would create phantom sieve.c:466 mappings that break C stepping.
        if (!mod.asm_lines.empty()) {
            namespace fs = std::filesystem;
            std::string ext;
            try { ext = fs::path(mod.file).extension().string(); } catch (...) {}
            bool is_asm = (ext == ".s" || ext == ".asm");
            if (is_asm) {
                auto resolved = resolve_source_path(mod.file);
                std::string asm_path = resolved ? *resolved : mod.file;
                for (const auto &[line_num, addr] : mod.asm_lines) {
                    std::string key = mod.name + ":" + std::to_string(line_num);
                    addr_by_file_line_.emplace(key, addr);
                    asm_by_addr_.emplace(addr, source_location{asm_path, line_num, true});
                }
            }
        }

        for (const auto &ln : mod.lines) {
            auto resolved = resolve_source_path(ln.file);
            std::string path = resolved ? *resolved : ln.file;
            source_by_addr_[ln.address] = {path, ln.line};

            std::string key = std::filesystem::path(path).filename().string()
                              + ":" + std::to_string(ln.line);
            addr_by_file_line_.emplace(key, ln.address);
        }
    }

    // MAP C$file$line$ symbols as fallback (don't overwrite CDB entries).
    for (const auto &sym : map_symbols_) {
        auto loc = map_symbol_to_source(sym);
        if (!loc) continue;
        uint16_t addr = static_cast<uint16_t>(sym.address & 0xFFFF);
        if (!source_by_addr_.count(addr)) {
            auto resolved = resolve_source_path(loc->file);
            std::string path = resolved ? *resolved : loc->file;
            source_by_addr_[addr] = {path, loc->line};
        }
        std::string key = std::filesystem::path(loc->file).filename().string()
                          + ":" + std::to_string(loc->line);
        addr_by_file_line_.emplace(key, addr);
    }
}

// ---------------------------------------------------------------------------
// Lookups (O(1) via index)
// ---------------------------------------------------------------------------

std::optional<source_location> dbg::lookup_source(uint16_t address) const
{
    auto it = source_by_addr_.find(address);
    if (it != source_by_addr_.end())
        return it->second;
    return std::nullopt;
}

std::optional<source_location> dbg::lookup_source_any(uint16_t address) const
{
    auto it = source_by_addr_.find(address);
    if (it != source_by_addr_.end())
        return it->second;
    auto ia = asm_by_addr_.find(address);
    if (ia != asm_by_addr_.end())
        return ia->second;
    return std::nullopt;
}

std::optional<uint16_t> dbg::lookup_address(const std::string &file, int line) const
{
    namespace fs = std::filesystem;

    // Primary lookup by full filename (e.g. "sieve.c:22" for C source).
    std::string fname = fs::path(file).filename().string();
    std::string key   = fname + ":" + std::to_string(line);
    auto it = addr_by_file_line_.find(key);
    if (it != addr_by_file_line_.end()) return it->second;

    // Fallback: match by stem only (e.g. "crt0:8" for crt0.s / crt0.asm).
    // This allows breakpoints in assembly files whose extension differs from
    // what the CDB module name implies.
    std::string stem     = fs::path(file).stem().string();
    std::string key_stem = stem + ":" + std::to_string(line);
    it = addr_by_file_line_.find(key_stem);
    if (it != addr_by_file_line_.end()) return it->second;

    return std::nullopt;
}

std::optional<std::string> dbg::lookup_symbol_exact(uint16_t address) const
{
    const sdcc::symbol *best = nullptr;
    for (const auto &sym : map_symbols_) {
        if ((sym.address & 0xFFFF) != address) continue;
        if (!best || (sym.name.size() > 1 && sym.name[0] == '_'))
            best = &sym;
    }
    return best ? std::optional{best->name} : std::nullopt;
}

std::optional<std::string> dbg::lookup_symbol(uint16_t address) const
{
    const sdcc::symbol *best = nullptr;
    uint16_t best_addr = 0;
    for (const auto &sym : map_symbols_) {
        uint16_t a = static_cast<uint16_t>(sym.address & 0xFFFF);
        if (a > address) continue;
        if (!best || a > best_addr) { best = &sym; best_addr = a; }
    }
    if (!best) return std::nullopt;
    std::ostringstream oss;
    oss << best->name;
    if (address > best_addr) oss << "+" << (address - best_addr);
    return oss.str();
}

std::optional<uint16_t> dbg::lookup_function_address(const std::string &name) const
{
    for (const auto &sym : map_symbols_) {
        // Underscore-prefixed C name (_funcname).
        if (sym.name == name || sym.name == "_" + name)
            return static_cast<uint16_t>(sym.address & 0xFFFF);

        // SDCC CDB-format MAP symbol: G$funcname$level$block
        // e.g. "G$clock_init$0$0".  Extract the name between first and second '$'.
        if (sym.name.size() > 3 && sym.name[0] == 'G' && sym.name[1] == '$') {
            auto d1 = sym.name.find('$', 2);
            if (d1 != std::string::npos && sym.name.substr(2, d1 - 2) == name)
                return static_cast<uint16_t>(sym.address & 0xFFFF);
        }
    }
    return std::nullopt;
}

std::optional<source_location> dbg::map_symbol_to_source(const sdcc::symbol &sym) const
{
    // SDCC/ASxxxx MAP symbol: C$<file>$<line>$...
    if (sym.name.size() < 4 || sym.name[0] != 'C' || sym.name[1] != '$')
        return std::nullopt;
    size_t p1 = sym.name.find('$', 2);
    if (p1 == std::string::npos) return std::nullopt;
    size_t p2 = sym.name.find('$', p1 + 1);
    if (p2 == std::string::npos) return std::nullopt;
    try {
        source_location loc;
        loc.file = sym.name.substr(2, p1 - 2);
        loc.line = std::stoi(sym.name.substr(p1 + 1, p2 - p1 - 1));
        return loc;
    } catch (...) {
        return std::nullopt;
    }
}

bool dbg::is_in_segment(uint16_t address) const
{
    for (const auto &seg : map_segments_) {
        uint16_t s = static_cast<uint16_t>(seg.address & 0xFFFF);
        uint16_t e = static_cast<uint16_t>((seg.address + seg.size) & 0xFFFF);
        if (seg.size > 0 && address >= s && address < e)
            return true;
    }
    return false;
}

std::optional<std::string> dbg::resolve_source_path(const std::string &path) const
{
    namespace fs = std::filesystem;
    fs::path p(path);
    std::string filename = p.filename().string();

    auto try_canonical = [](const fs::path &candidate) -> std::optional<std::string> {
        try {
            if (fs::exists(candidate))
                return fs::canonical(candidate).string();
        } catch (...) {}
        return std::nullopt;
    };

    if (p.is_absolute()) {
        if (auto r = try_canonical(p)) return r;
    }
    if (auto r = try_canonical(p)) return r;

    std::vector<fs::path> roots;
    if (!source_root_.empty()) roots.push_back(source_root_);
    for (const auto &r : source_roots_) roots.push_back(r);

    std::unordered_set<std::string> seen;
    for (const auto &root : roots) {
        if (!seen.insert(root.string()).second) continue;
        if (auto r = try_canonical(root / p))        return r;
        if (auto r = try_canonical(root / filename)) return r;
        try {
            for (const auto &entry : fs::directory_iterator(root)) {
                if (!entry.is_directory()) continue;
                if (auto r = try_canonical(entry.path() / filename)) return r;
            }
        } catch (...) {}
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Source file content cache
// ---------------------------------------------------------------------------

void dbg::clear_source_cache()
{
    source_ref_to_content_.clear();
    source_path_to_ref_.clear();
    next_source_reference_ = 1000;
    clear_full_listing();
}

int dbg::ensure_source_reference(const std::string &path,
                                 const std::string &mime_type)
{
    namespace fs = std::filesystem;
    auto resolved = resolve_source_path(path);
    std::string lookup = resolved ? *resolved : path;

    auto it = source_path_to_ref_.find(lookup);
    if (it != source_path_to_ref_.end()) return it->second;

    std::ifstream ifs(lookup);
    if (!ifs) return 0;

    std::ostringstream content;
    content << ifs.rdbuf();

    int ref = next_source_reference_++;
    source_content sc;
    sc.path      = lookup;
    sc.name      = fs::path(lookup).filename().string();
    sc.content   = content.str();
    sc.mime_type = mime_type;

    source_ref_to_content_[ref] = std::move(sc);
    source_path_to_ref_[lookup] = ref;
    return ref;
}

std::optional<source_content> dbg::source_by_reference(int source_reference) const
{
    auto it = source_ref_to_content_.find(source_reference);
    if (it == source_ref_to_content_.end()) return std::nullopt;
    return it->second;
}

void dbg::build_full_listing()
{
    full_listing_content_.clear();
    full_listing_addrs_.clear();
    full_listing_addr_to_line_.clear();

    std::ostringstream oss;
    uint32_t addr = 0;
    int      line = 1;

    while (addr < memory_.size()) {
        char    buf[64];
        int     t1 = 0, t2 = 0;
        int     ilen = z80ex_dasm(buf, sizeof(buf), 0, &t1, &t2,
                                   dasm_readbyte_cb, addr, &memory_);
        if (ilen <= 0) ilen = 1;

        full_listing_addrs_.push_back(static_cast<uint16_t>(addr));
        full_listing_addr_to_line_[static_cast<uint16_t>(addr)] = line;

        oss << "    " << std::uppercase << std::setfill('0')
            << std::setw(6) << std::hex << addr << " ";

        constexpr int kByteCols = 12;
        int bc = 0;
        for (int j = 0; j < ilen && (addr + j) < memory_.size(); ++j) {
            oss << std::setw(2) << std::setfill('0') << std::hex
                << static_cast<int>(memory_[addr + j]) << " ";
            bc += 3;
        }
        for (; bc < kByteCols; ++bc) oss << " ";

        oss << "  [" << std::right << std::setw(2) << std::dec << t1 << "]   ";
        oss << symbolize_asm_line(buf) << "\n";

        addr += static_cast<uint32_t>(ilen);
        ++line;
    }

    full_listing_content_ = oss.str();
    full_listing_built_   = true;
}

// ---------------------------------------------------------------------------
// Disassembler callback + formatting
// ---------------------------------------------------------------------------

uint8_t dbg::dasm_readbyte_cb(Z80EX_WORD addr, void *user_data)
{
    auto *mem = static_cast<std::vector<uint8_t> *>(user_data);
    return (addr < mem->size()) ? (*mem)[addr] : 0xFF;
}

std::string dbg::format_hex(uint16_t value, int width)
{
    return std::format("0x{:0{}X}", value, width);
}

// Replace a single regex match group with its MAP symbol name (if exact hit).
static std::string replace_hex_operands(const std::string &input,
                                         const std::regex &pattern,
                                         int capture_group,
                                         const dbg &ctx)
{
    std::string out;
    std::sregex_iterator it(input.begin(), input.end(), pattern);
    std::sregex_iterator end;
    size_t last = 0;
    for (; it != end; ++it) {
        const auto &m = *it;
        out.append(input.substr(last, static_cast<size_t>(m.position()) - last));
        try {
            uint16_t addr = static_cast<uint16_t>(
                std::stoul(m.str(capture_group), nullptr, 16));
            auto sym = ctx.lookup_symbol_exact(addr);
            if (sym) {
                if (!m.str().empty() && m.str()[0] == '#') out.append("#");
                out.append(*sym);
            } else {
                out.append(m.str());
            }
        } catch (...) {
            out.append(m.str());
        }
        last = static_cast<size_t>(m.position() + m.length());
    }
    out.append(input.substr(last));
    return out;
}

std::string dbg::symbolize_asm_line(const std::string &line) const
{
    // Skip 8-bit immediate loads — the operand is a value, not an address.
    static const std::regex r_ld8(
        R"(LD\s+(?:[A-E]|H|L|IXH|IXL|IYH|IYL|\([A-Z]+(?:[+-][0-9]+)?\))\s*,\s*#)",
        std::regex::icase);
    if (std::regex_search(line, r_ld8)) return line;

    static const std::regex r_0x    (R"(0x([0-9A-Fa-f]{1,4}))");
    static const std::regex r_dollar(R"(\$([0-9A-Fa-f]{1,4}))");
    static const std::regex r_hsuf  (R"(\b([0-9A-Fa-f]{1,4})[Hh]\b)");
    static const std::regex r_hash  (R"(#([0-9A-Fa-f]{1,4}))");

    std::string s = replace_hex_operands(line, r_0x,     1, *this);
    s             = replace_hex_operands(s,    r_dollar, 1, *this);
    s             = replace_hex_operands(s,    r_hsuf,   1, *this);
    s             = replace_hex_operands(s,    r_hash,   1, *this);
    return s;
}

// ---------------------------------------------------------------------------
// C variable support
// ---------------------------------------------------------------------------

const sdcc::cdbg_info_function *dbg::lookup_function_at(uint16_t address) const
{
    // Strategy 1: use start_addr/end_addr populated directly from the CDB
    // L:F/L:XF/L:G/L:XG records.  This works for both static (F-scope) and
    // global (G-scope) functions without needing MAP data.
    for (const auto &mod : cdb_modules_) {
        for (const auto &fn : mod.functions) {
            if (!fn.has_addr()) continue;
            if (address < fn.start_addr) continue;
            // end_addr = 0xFFFF means "not recorded" — treat as open-ended.
            if (fn.end_addr != 0xFFFF && address >= fn.end_addr) continue;
            return &fn;
        }
    }

    // Strategy 2 (MAP fallback): nearest symbol ≤ address.
    // Covers the case where CDB L: markers were absent or mismatched.
    const sdcc::cdbg_info_function *best = nullptr;
    uint16_t best_start = 0;
    for (const auto &mod : cdb_modules_) {
        for (const auto &fn : mod.functions) {
            auto fn_addr = lookup_function_address(fn.name);
            if (!fn_addr || *fn_addr > address) continue;
            if (!best || *fn_addr >= best_start) {
                best_start = *fn_addr;
                best = &fn;
            }
        }
    }
    return best;
}

std::string dbg::c_variable_value(const sdcc::cdbg_info_symbol &sym) const
{
    using sc = sdcc::storage_class;
    int sz = std::max(1, std::min(sym.size, 4));

    auto read_mem = [&](uint16_t addr) -> std::string {
        uint32_t val = 0;
        for (int i = 0; i < sz && (addr + i) < memory_.size(); ++i)
            val |= static_cast<uint32_t>(memory_[addr + i]) << (8 * i);
        // Show as signed decimal when type_info indicates signed integer.
        bool is_signed = sym.type_info.find('S') != std::string::npos &&
                         sym.type_info.find("SC") == std::string::npos;
        if (is_signed && sz <= 2) {
            int16_t sv = (sz == 1)
                ? static_cast<int16_t>(static_cast<int8_t>(val & 0xFF))
                : static_cast<int16_t>(val & 0xFFFF);
            return std::to_string(sv)
                   + " (" + format_hex(static_cast<uint16_t>(val & 0xFFFF), sz * 2) + ")";
        }
        return format_hex(static_cast<uint16_t>(val & 0xFFFF), sz * 2);
    };

    if (sym.storage == sc::stack) {
        // IX-relative local variable.
        uint16_t ix   = static_cast<uint16_t>(z80ex_get_reg(cpu_, regIX));
        uint16_t addr = static_cast<uint16_t>(ix + sym.offset);
        return read_mem(addr);
    }

    if (sym.storage == sc::internal || sym.storage == sc::ext ||
        sym.storage == sc::global_auto) {
        // Static / global — address from MAP symbols.
        for (const auto &ms : map_symbols_) {
            if (ms.name == "_" + sym.name || ms.name == sym.name) {
                uint16_t addr = static_cast<uint16_t>(ms.address & 0xFFFF);
                return read_mem(addr);
            }
        }
    }

    if (sym.storage == sc::reg) {
        // Register variable.  SDCC encodes the register(s) as a comma-separated
        // list of lowercase Z80 register names inside brackets: [c], [c,b], [e,d].
        // Registers are listed low-byte first, so [c,b] means BC (C=lo, B=hi).
        if (!sym.reg_list.empty()) {
            uint16_t val = 0;
            const std::string &r = sym.reg_list;

            // 16-bit pairs (lo,hi order).
            if      (r == "c,b") val = z80ex_get_reg(cpu_, regBC);
            else if (r == "e,d") val = z80ex_get_reg(cpu_, regDE);
            else if (r == "l,h") val = z80ex_get_reg(cpu_, regHL);
            // 8-bit registers.
            else if (r == "a")   val = (z80ex_get_reg(cpu_, regAF) >> 8) & 0xFF;
            else if (r == "b")   val = (z80ex_get_reg(cpu_, regBC) >> 8) & 0xFF;
            else if (r == "c")   val =  z80ex_get_reg(cpu_, regBC)       & 0xFF;
            else if (r == "d")   val = (z80ex_get_reg(cpu_, regDE) >> 8) & 0xFF;
            else if (r == "e")   val =  z80ex_get_reg(cpu_, regDE)       & 0xFF;
            else if (r == "h")   val = (z80ex_get_reg(cpu_, regHL) >> 8) & 0xFF;
            else if (r == "l")   val =  z80ex_get_reg(cpu_, regHL)       & 0xFF;
            else if (r == "ixl") val =  z80ex_get_reg(cpu_, regIX)       & 0xFF;
            else if (r == "ixh") val = (z80ex_get_reg(cpu_, regIX) >> 8) & 0xFF;
            else                 return "<" + r + ">";  // unknown, show name

            return format_hex(val, sz * 2);
        }
        return "<reg>";
    }

    return "?";
}
