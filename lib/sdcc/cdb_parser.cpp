// cdb_parser.cpp
// CDB parser for SDCC compiler output.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <sstream>
#include <string_view>
#include <algorithm>

#include <sdcc/cdb_parser.h>
#include <sdcc/util.h>

namespace sdcc {

std::optional<std::vector<cdbg_info_module>> cdb_parser::parse(const std::string &path) {
    auto lines = util::read_lines(path);
    if (!lines) return std::nullopt;

    data_.clear();
    asm_lines_.clear();
    current_module_     = {};
    current_module_ptr_ = nullptr;

    for (const auto &line : *lines) {
        auto trimmed = util::trim(line);
        if (!trimmed.empty())
            parse_line(trimmed);
    }
    merge_asm_lines();
    return data_;
}

void cdb_parser::merge_asm_lines() {
    // Merge A$ records into existing modules, or create stub modules for
    // pure-assembly files (like crt0) that have no M: record in the CDB.
    for (auto &[mod_name, line_map] : asm_lines_) {
        cdbg_info_module *target = nullptr;
        for (auto &mod : data_) {
            if (mod.name == mod_name) { target = &mod; break; }
        }
        if (!target) {
            cdbg_info_module stub;
            stub.name = mod_name;
            stub.file = mod_name + ".s"; // best-guess extension
            data_.push_back(std::move(stub));
            target = &data_.back();
            // current_module_ptr_ is only used during parsing, which is
            // already finished, so no need to fix it here.
        }
        for (auto &[line, addr] : line_map)
            target->asm_lines.emplace(line, addr);
    }
}

void cdb_parser::parse_line(std::string_view line) {
    if (line.size() < 2 || line[1] != ':') return;

    char type = line[0];
    std::string_view content = line.substr(2);

    switch (type) {
        case 'M': parse_module(content);    break;
        case 'F': parse_function(content);  break;
        case 'S': parse_symbol(content);    break;
        case 'T': parse_type(content);      break;
        case 'L': parse_line_info(content); break;
        default:  break;
    }
}

void cdb_parser::parse_module(std::string_view content) {
    cdbg_info_module mod;
    mod.name = std::string(content);
    mod.file = mod.name + ".c";
    current_module_ = mod.name;
    data_.push_back(std::move(mod));
    current_module_ptr_ = &data_.back();
}

void cdb_parser::parse_function(std::string_view content) {
    if (!current_module_ptr_) return;

    cdbg_info_function func;
    func.scope = (content[0] == 'G') ? "global" : "local";

    size_t d1 = content.find('$');
    if (d1 == std::string_view::npos) return;
    size_t d2 = content.find('$', d1 + 1);
    if (d2 == std::string_view::npos) return;

    func.name = std::string(content.substr(d1 + 1, d2 - d1 - 1));
    // Remove "module." prefix if present.
    if (auto dot = func.name.find('.'); dot != std::string::npos)
        func.name = func.name.substr(dot + 1);

    current_module_ptr_->functions.push_back(std::move(func));
}

void cdb_parser::parse_symbol(std::string_view content) {
    if (!current_module_ptr_) return;

    char scope_char = content[0];
    size_t d1 = content.find('$');
    if (d1 == std::string_view::npos) return;

    std::string_view scope_prefix = content.substr(1, d1 - 1);

    size_t d2 = content.find('$', d1 + 1);
    if (d2 == std::string_view::npos) return;

    cdbg_info_symbol symbol;
    symbol.name = std::string(content.substr(d1 + 1, d2 - d1 - 1));

    size_t p1 = content.find('(');
    if (p1 != std::string_view::npos) {
        size_t p2 = content.find(')', p1);
        if (p2 != std::string_view::npos) {
            symbol.type_info = std::string(content.substr(p1 + 1, p2 - p1 - 1));

            // Parse byte size from "{N}" prefix of type_info.
            if (!symbol.type_info.empty() && symbol.type_info[0] == '{') {
                auto close = symbol.type_info.find('}');
                if (close != std::string::npos) {
                    try { symbol.size = std::stoi(symbol.type_info.substr(1, close - 1)); }
                    catch (...) {}
                }
            }
            symbol.size = std::max(1, std::min(symbol.size, 4));

            // Storage info after closing ')':
            //   ",B,bank,offset"          — stack/IX-relative
            //   ",R,bank,offset,[regs]"   — register(s), e.g. [c] or [c,b]
            if (p2 + 1 < content.size()) {
                std::string_view suffix = content.substr(p2 + 1);

                // Extract register list from "[...]" if present (before split
                // so commas inside brackets don't confuse the field parser).
                auto bracket = suffix.find('[');
                if (bracket != std::string_view::npos) {
                    auto bend = suffix.find(']', bracket);
                    if (bend != std::string_view::npos)
                        symbol.reg_list = std::string(
                            suffix.substr(bracket + 1, bend - bracket - 1));
                }

                auto parts = util::split(suffix, ',');
                // parts[0]="" [1]=storage_class [2]=bank [3]=offset
                if (parts.size() >= 2 && !parts[1].empty()) {
                    char sc = parts[1][0];
                    switch (sc) {
                        case 'B': symbol.storage = sdcc::storage_class::stack;       break;
                        case 'R': symbol.storage = sdcc::storage_class::reg;         break;
                        case 'I': symbol.storage = sdcc::storage_class::internal;    break;
                        case 'E': symbol.storage = sdcc::storage_class::ext;         break;
                        case 'A': symbol.storage = sdcc::storage_class::global_auto; break;
                        default:  break;
                    }
                }
                if (parts.size() >= 4 && !parts[3].empty()) {
                    try { symbol.offset = std::stoi(std::string(parts[3])); }
                    catch (...) {}
                }
            }
        }
    }

    if (scope_char == 'L') {
        symbol.scope = "local";
        auto dot = scope_prefix.find('.');
        if (dot == std::string_view::npos) return;
        std::string func_name(scope_prefix.substr(dot + 1));
        for (auto &func : current_module_ptr_->functions) {
            if (func.name == func_name) {
                func.local_symbols.push_back(std::move(symbol));
                return;
            }
        }
    } else if (scope_char == 'G') {
        symbol.scope = "global";
        current_module_ptr_->global_symbols.push_back(std::move(symbol));
    } else {
        symbol.scope = (scope_char == 'F') ? "local" : "struct";
        current_module_ptr_->global_symbols.push_back(std::move(symbol));
    }
}

void cdb_parser::parse_type(std::string_view content) {
    if (!current_module_ptr_) return;

    cdbg_info_type type;
    type.scope = (content[0] == 'G') ? "global" : "local";

    size_t d1 = content.find('$');
    if (d1 == std::string_view::npos) return;
    size_t bracket = content.find('[');
    if (bracket == std::string_view::npos) return;

    std::string full_name = std::string(content.substr(d1 + 1, bracket - d1 - 1));
    if (auto dot = full_name.find('.'); dot != std::string::npos)
        type.name = full_name.substr(dot + 1);
    else
        type.name = full_name;

    size_t end_bracket = content.find_last_of(']');
    if (end_bracket != std::string_view::npos)
        type.type_info = std::string(content.substr(bracket, end_bracket - bracket + 1));

    current_module_ptr_->types.push_back(std::move(type));
}

// Parse a function start/end marker from an L: record.
// content examples:
//   Fsieve$run_sieve$0$0:70   (static function start)
//   XFsieve$run_sieve$0$0:BE  (static function end)
//   $main$0$0:BF              (global function — 'G' already stripped)
//   X$main$0$0:DC             (global function end — 'G' already stripped)
void cdb_parser::parse_func_marker(std::string_view content, bool is_end)
{
    // Address is after the last ':'.
    auto colon = content.rfind(':');
    if (colon == std::string_view::npos) return;
    uint16_t addr = 0;
    try { addr = static_cast<uint16_t>(
            std::stoul(std::string(content.substr(colon + 1)), nullptr, 16)); }
    catch (...) { return; }

    // Extract module name (before first '$') and function name (between 1st and 2nd '$').
    auto d1 = content.find('$');
    if (d1 == std::string_view::npos) return;
    auto d2 = content.find('$', d1 + 1);
    if (d2 == std::string_view::npos) return;

    std::string_view mod_sv  = content.substr(0, d1);       // e.g. "sieve" or ""
    std::string       fn_name(content.substr(d1 + 1, d2 - d1 - 1)); // e.g. "run_sieve"

    // Search all modules (L: records may appear outside the owning M: block).
    for (auto &mod : data_) {
        if (!mod_sv.empty() && mod.name != std::string(mod_sv)) continue;
        for (auto &fn : mod.functions) {
            if (fn.name == fn_name) {
                if (!is_end) fn.start_addr = addr;
                else         fn.end_addr   = addr;
                return;
            }
        }
    }
}

void cdb_parser::parse_line_info(std::string_view content) {
    if (content.empty()) return;

    // Function start/end markers:
    //   Fsieve$funcname$...:addr   — static function start
    //   XFsieve$funcname$...:addr  — static function end
    //   G$funcname$...:addr        — global function start (also used for global vars)
    //   XG$funcname$...:addr       — global function end
    if (content[0] == 'F') {
        parse_func_marker(content.substr(1), false); // strip 'F'
        return;
    }
    if (content.size() >= 2 && content[0] == 'X' && content[1] == 'F') {
        parse_func_marker(content.substr(2), true); // strip 'XF'
        return;
    }
    if (content[0] == 'G') {
        parse_func_marker(content.substr(1), false); // strip 'G'; module part is empty ("")
        return;
    }
    if (content.size() >= 2 && content[0] == 'X' && content[1] == 'G') {
        parse_func_marker(content.substr(2), true);  // strip 'XG'
        return;
    }

    // Assembly line: A$module$linenum:hex_address
    if (content[0] == 'A') {
        auto d1 = content.find('$');
        if (d1 == std::string_view::npos) return;
        auto d2 = content.find('$', d1 + 1);
        if (d2 == std::string_view::npos) return;
        auto colon = content.rfind(':');
        if (colon == std::string_view::npos || colon <= d2) return;

        std::string mod_name(content.substr(d1 + 1, d2 - d1 - 1));
        int line_num = 0;
        try { line_num = std::stoi(std::string(content.substr(d2 + 1, colon - d2 - 1))); }
        catch (...) { return; }
        uint16_t addr = 0;
        try { addr = static_cast<uint16_t>(
                std::stoul(std::string(content.substr(colon + 1)), nullptr, 16)); }
        catch (...) { return; }

        // Accumulate in asm_lines_ — merged into modules after all parsing.
        asm_lines_[mod_name].emplace(line_num, addr);
        return;
    }

    // Only C source lines after this point: C$file$line$level_block:hex_address
    if (content[0] != 'C') return;

    auto parts = util::split(content, '$');
    // parts[0]="C", parts[1]=file, parts[2]=line, parts[3]=level_block,
    // parts[4]=... last token contains ":hex_addr"
    if (parts.size() < 4) return;

    cdbg_info_line ln;
    ln.scope = "local";
    ln.file  = std::string(parts[1]);
    std::replace(ln.file.begin(), ln.file.end(), '\\', '/');

    try {
        ln.line = std::stoi(std::string(parts[2]));
    } catch (...) { return; }

    // Address is after ':' in the last part.
    std::string_view last = parts.back();
    if (auto colon = last.find(':'); colon != std::string_view::npos) {
        try {
            ln.address = static_cast<uint16_t>(
                std::stoul(std::string(last.substr(colon + 1)), nullptr, 16));
        } catch (...) {}
    }

    // Match module by filename stem (L: records can appear after a
    // different M: record).
    std::string_view file_sv = ln.file;
    std::string mod_name;
    if (auto dot = file_sv.rfind('.'); dot != std::string_view::npos)
        mod_name = std::string(file_sv.substr(0, dot));

    // Fast path: still in the same module.
    if (current_module_ptr_ && current_module_ptr_->name == mod_name) {
        if (current_module_ptr_->file == current_module_ptr_->name + ".c")
            current_module_ptr_->file = ln.file;
        current_module_ptr_->lines.push_back(ln);
        return;
    }

    // Slow path: find the matching module.
    for (auto &mod : data_) {
        if (mod.name == mod_name) {
            if (mod.file == mod.name + ".c") mod.file = ln.file;
            mod.lines.push_back(ln);
            return;
        }
    }
}

} // namespace sdcc
