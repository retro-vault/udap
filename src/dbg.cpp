// dbg.cpp
// Debug Adapter Protocol (DAP) server implementation for Z80 emulation.
//
// This file implements the core runtime functions of the `dbg` class used
// for handling Debug Adapter Protocol requests, formatting data, and
// registering handlers with the DAP dispatcher.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <dbg.h>
#include <dap/handler.h>
#include <unordered_set>

namespace {

std::optional<source_location> map_symbol_to_source(const sdcc::symbol &sym)
{
    // SDCC/ASxxxx map symbol format:
    // C$<file>$<line>$...
    if (sym.name.size() < 4 || sym.name[0] != 'C' || sym.name[1] != '$')
        return std::nullopt;

    size_t p1 = sym.name.find('$', 2);
    if (p1 == std::string::npos)
        return std::nullopt;
    size_t p2 = sym.name.find('$', p1 + 1);
    if (p2 == std::string::npos)
        return std::nullopt;

    source_location loc;
    loc.file = sym.name.substr(2, p1 - 2);
    try
    {
        loc.line = std::stoi(sym.name.substr(p1 + 1, p2 - p1 - 1));
    }
    catch (...)
    {
        return std::nullopt;
    }
    return loc;
}

} // namespace

// Forward declarations of handler factory functions (defined in handlers/).
namespace handlers {
    std::unique_ptr<dap::request_handler> make_initialize(dbg &ctx);
    std::unique_ptr<dap::request_handler> make_launch(dbg &ctx);
    std::unique_ptr<dap::request_handler> make_configuration_done(dbg &ctx);
    std::unique_ptr<dap::request_handler> make_threads(dbg &ctx);
    std::unique_ptr<dap::request_handler> make_stack_trace(dbg &ctx);
    std::unique_ptr<dap::request_handler> make_scopes(dbg &ctx);
    std::unique_ptr<dap::request_handler> make_variables(dbg &ctx);
    std::unique_ptr<dap::request_handler> make_continue(dbg &ctx);
    std::unique_ptr<dap::request_handler> make_next(dbg &ctx);
    std::unique_ptr<dap::request_handler> make_step_in(dbg &ctx);
    std::unique_ptr<dap::request_handler> make_step_out(dbg &ctx);
    std::unique_ptr<dap::request_handler> make_set_breakpoints(dbg &ctx);
    std::unique_ptr<dap::request_handler> make_set_instruction_breakpoints(dbg &ctx);
    std::unique_ptr<dap::request_handler> make_source(dbg &ctx);
    std::unique_ptr<dap::request_handler> make_read_memory(dbg &ctx);
    std::unique_ptr<dap::request_handler> make_disconnect(dbg &ctx);
    std::unique_ptr<dap::request_handler> make_set_exception_breakpoints(dbg &ctx);
    std::unique_ptr<dap::request_handler> make_pause(dbg &ctx);
    std::unique_ptr<dap::request_handler> make_disassemble(dbg &ctx);
    std::unique_ptr<dap::request_handler> make_set_function_breakpoints(dbg &ctx);
    std::unique_ptr<dap::request_handler> make_breakpoint_locations(dbg &ctx);
    std::unique_ptr<dap::request_handler> make_loaded_sources(dbg &ctx);
    std::unique_ptr<dap::request_handler> make_evaluate(dbg &ctx);
}

void dbg::register_handlers(dap::dap &dispatcher)
{
    dispatcher.add_handler(handlers::make_initialize(*this));
    dispatcher.add_handler(handlers::make_launch(*this));
    dispatcher.add_handler(handlers::make_configuration_done(*this));
    dispatcher.add_handler(handlers::make_threads(*this));
    dispatcher.add_handler(handlers::make_stack_trace(*this));
    dispatcher.add_handler(handlers::make_scopes(*this));
    dispatcher.add_handler(handlers::make_variables(*this));
    dispatcher.add_handler(handlers::make_continue(*this));
    dispatcher.add_handler(handlers::make_next(*this));
    dispatcher.add_handler(handlers::make_step_in(*this));
    dispatcher.add_handler(handlers::make_step_out(*this));
    dispatcher.add_handler(handlers::make_set_breakpoints(*this));
    dispatcher.add_handler(handlers::make_set_instruction_breakpoints(*this));
    dispatcher.add_handler(handlers::make_source(*this));
    dispatcher.add_handler(handlers::make_read_memory(*this));
    dispatcher.add_handler(handlers::make_disconnect(*this));
    dispatcher.add_handler(handlers::make_set_exception_breakpoints(*this));
    dispatcher.add_handler(handlers::make_pause(*this));
    dispatcher.add_handler(handlers::make_disassemble(*this));
    dispatcher.add_handler(handlers::make_set_function_breakpoints(*this));
    dispatcher.add_handler(handlers::make_breakpoint_locations(*this));
    dispatcher.add_handler(handlers::make_loaded_sources(*this));
    dispatcher.add_handler(handlers::make_evaluate(*this));
}

void dbg::set_event_sender(std::function<void(const std::string &)> sender)
{
    send_event_ = std::move(sender);
}

void dbg::send_event(const std::string &event_json)
{
    if (send_event_)
        send_event_(event_json);
}

std::string dbg::format_hex(uint16_t value, int width)
{
    std::ostringstream oss;
    oss << std::uppercase << std::setfill('0') << std::setw(width)
        << std::hex << value;
    return "0x" + oss.str();
}

std::optional<source_location> dbg::lookup_source(uint16_t address) const
{
    for (auto &mod : cdb_modules_)
    {
        for (auto &ln : mod.lines)
        {
            if (ln.address != address)
                continue;

            auto resolved = resolve_source_path(ln.file);
            if (resolved)
                return source_location{*resolved, ln.line};

            return source_location{ln.file, ln.line}; // unresolved fallback
        }
    }

    // Fallback: map C$<file>$<line>$... symbols from MAP/NOI style names.
    for (const auto &sym : map_symbols_)
    {
        if (sym.address != address)
            continue;

        auto loc = map_symbol_to_source(sym);
        if (!loc)
            continue;

        auto resolved = resolve_source_path(loc->file);
        if (resolved)
            return source_location{*resolved, loc->line};

        return loc;
    }
    return std::nullopt;
}

std::optional<uint16_t> dbg::lookup_address(const std::string &file, int line) const
{
    namespace fs = std::filesystem;
    std::string query_name = fs::path(file).filename().string();

    for (auto &mod : cdb_modules_)
    {
        for (auto &ln : mod.lines)
        {
            if (ln.line != line)
                continue;

            // Match by bare filename since CDB stores bare names
            // and the DAP request sends full paths.
            std::string cdb_name = fs::path(ln.file).filename().string();
            if (cdb_name == query_name)
                return ln.address;
        }
    }

    // Fallback: MAP symbols.
    for (const auto &sym : map_symbols_)
    {
        auto loc = map_symbol_to_source(sym);
        if (!loc || loc->line != line)
            continue;

        std::string map_name = fs::path(loc->file).filename().string();
        if (map_name == query_name)
            return static_cast<uint16_t>(sym.address & 0xFFFF);
    }
    return std::nullopt;
}

std::optional<std::string> dbg::lookup_symbol_exact(uint16_t address) const
{
    const sdcc::symbol *best = nullptr;
    for (const auto &sym : map_symbols_)
    {
        uint16_t sym_addr = static_cast<uint16_t>(sym.address & 0xFFFF);
        if (sym_addr != address)
            continue;

        if (!best)
            best = &sym;
        else if (sym.name.size() > 1 && sym.name[0] == '_')
            best = &sym; // prefer C-like symbol names for display
    }

    if (!best)
        return std::nullopt;
    return best->name;
}

std::optional<std::string> dbg::lookup_symbol(uint16_t address) const
{
    const sdcc::symbol *best = nullptr;
    uint16_t best_addr = 0;

    for (const auto &sym : map_symbols_)
    {
        uint16_t sym_addr = static_cast<uint16_t>(sym.address & 0xFFFF);
        if (sym_addr > address)
            continue;

        if (!best || sym_addr > best_addr)
        {
            best = &sym;
            best_addr = sym_addr;
        }
    }

    if (!best)
        return std::nullopt;

    std::ostringstream oss;
    oss << best->name;
    if (address > best_addr)
        oss << "+" << (address - best_addr);
    return oss.str();
}

std::optional<uint16_t> dbg::lookup_function_address(const std::string &name) const
{
    // Search MAP symbols: exact match or with _ prefix (SDCC convention).
    for (const auto &sym : map_symbols_)
    {
        if (sym.name == name || sym.name == "_" + name)
            return static_cast<uint16_t>(sym.address & 0xFFFF);
    }
    return std::nullopt;
}

bool dbg::is_in_segment(uint16_t address) const
{
    for (const auto &seg : map_segments_)
    {
        uint16_t seg_start = static_cast<uint16_t>(seg.address & 0xFFFF);
        uint16_t seg_end = static_cast<uint16_t>((seg.address + seg.size) & 0xFFFF);
        if (seg.size > 0 && address >= seg_start && address < seg_end)
            return true;
    }
    return false;
}

std::optional<std::string> dbg::resolve_source_path(const std::string &path) const
{
    namespace fs = std::filesystem;
    fs::path p(path);
    std::string filename = p.filename().string();

    auto canonical_if_exists = [](const fs::path &candidate)
        -> std::optional<std::string>
    {
        try
        {
            if (fs::exists(candidate))
                return fs::canonical(candidate).string();
        }
        catch (...) {}
        return std::nullopt;
    };

    if (p.is_absolute())
    {
        auto resolved = canonical_if_exists(p);
        if (resolved)
            return resolved;
    }

    auto cwd_resolved = canonical_if_exists(p);
    if (cwd_resolved)
        return cwd_resolved;

    std::vector<fs::path> roots;
    if (!source_root_.empty())
        roots.push_back(source_root_);
    for (const auto &root : source_roots_)
        roots.push_back(root);

    std::unordered_set<std::string> seen_roots;
    for (auto &root : roots)
    {
        std::string key = root.string();
        if (key.empty() || seen_roots.count(key))
            continue;
        seen_roots.insert(key);

        auto resolved = canonical_if_exists(root / p);
        if (resolved)
            return resolved;

        resolved = canonical_if_exists(root / filename);
        if (resolved)
            return resolved;

        // One-level subdirectory match: root/src/main.c.
        try
        {
            for (auto &entry : fs::directory_iterator(root))
            {
                if (!entry.is_directory())
                    continue;
                resolved = canonical_if_exists(entry.path() / filename);
                if (resolved)
                    return resolved;
            }
        }
        catch (...) {}
    }

    return std::nullopt;
}

void dbg::set_source_breakpoints_for_file(const std::string &file,
                                          std::vector<int> lines)
{
    if (file.empty())
        return;
    if (lines.empty())
    {
        source_breakpoints_by_file_.erase(file);
        return;
    }
    source_breakpoints_by_file_[file] = std::move(lines);
}

std::vector<nlohmann::json> dbg::resolve_source_breakpoints_for_file(
    const std::string &file) const
{
    std::vector<nlohmann::json> out;
    auto it = source_breakpoints_by_file_.find(file);
    if (it == source_breakpoints_by_file_.end())
        return out;

    for (int line : it->second)
    {
        auto addr = lookup_address(file, line);
        if (addr)
        {
            out.push_back({{"verified", true}, {"line", line}});
        }
        else if (!has_cdb() && !has_map())
        {
            out.push_back({{"verified", false},
                           {"line", line},
                           {"message", "Pending symbol resolution (CDB/MAP not loaded yet)"}});
        }
        else
        {
            out.push_back({{"verified", false},
                           {"line", line},
                           {"message", "No address mapping for this line"}});
        }
    }
    return out;
}

void dbg::rebuild_source_breakpoint_addresses()
{
    breakpoints_.clear();
    std::unordered_set<uint16_t> seen;

    for (const auto &entry : source_breakpoints_by_file_)
    {
        const auto &file = entry.first;
        for (int line : entry.second)
        {
            auto addr = lookup_address(file, line);
            if (!addr)
                continue;
            if (seen.insert(*addr).second)
                breakpoints_.push_back(*addr);
        }
    }
}

void dbg::clear_source_cache()
{
    source_ref_to_content_.clear();
    source_path_to_ref_.clear();
    next_source_reference_ = 1000;
}

int dbg::ensure_source_reference(const std::string &path,
                                 const std::string &mime_type)
{
    namespace fs = std::filesystem;
    auto resolved = resolve_source_path(path);
    std::string lookup_path = resolved ? *resolved : path;

    auto it = source_path_to_ref_.find(lookup_path);
    if (it != source_path_to_ref_.end())
        return it->second;

    std::ifstream ifs(lookup_path);
    if (!ifs)
        return 0;

    std::ostringstream content;
    content << ifs.rdbuf();

    int source_ref = next_source_reference_++;
    source_content sc;
    sc.path = lookup_path;
    sc.name = fs::path(lookup_path).filename().string();
    sc.content = content.str();
    sc.mime_type = mime_type;

    source_ref_to_content_[source_ref] = std::move(sc);
    source_path_to_ref_[lookup_path] = source_ref;
    return source_ref;
}

std::optional<source_content> dbg::source_by_reference(int source_reference) const
{
    auto it = source_ref_to_content_.find(source_reference);
    if (it == source_ref_to_content_.end())
        return std::nullopt;
    return it->second;
}

uint8_t dbg::dasm_readbyte_cb(Z80EX_WORD addr, void *user_data)
{
    auto *memory = static_cast<std::vector<uint8_t> *>(user_data);
    if (addr < memory->size())
        return (*memory)[addr];
    else
        return 0xFF;
}
