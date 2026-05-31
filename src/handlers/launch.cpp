// launch.cpp — DAP "launch" request handler.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <fstream>
#include <filesystem>
#include <dap/dap.h>
#include <dap/handler.h>
#include <sdcc/cdb_parser.h>
#include <sdcc/map_parser.h>
#include <dbg.h>

namespace {

struct ihx_load_result {
    uint16_t entry = 0;
    bool explicit_start = false;
};

std::optional<uint16_t> parse_start_address_arg(const nlohmann::json &args)
{
    if (!args.contains("startAddress"))
        return std::nullopt;

    const auto &value = args["startAddress"];
    try
    {
        if (value.is_number_integer() || value.is_number_unsigned())
            return static_cast<uint16_t>(value.get<uint32_t>() & 0xFFFF);

        if (value.is_string())
        {
            const auto s = value.get<std::string>();
            if (s.empty())
                return std::nullopt;
            return static_cast<uint16_t>(std::stoul(s, nullptr, 0) & 0xFFFF);
        }
    }
    catch (...) {}

    return std::nullopt;
}

// Parse an Intel HEX (.ihx/.hex) stream into a flat memory buffer.
// Prefers explicit start address records (types 03/05); otherwise falls back
// to the lowest data address.
ihx_load_result load_ihx(std::istream &in, std::vector<uint8_t> &mem)
{
    uint32_t upper_base = 0;
    uint32_t lowest_data_addr = 0xFFFFFFFF;
    std::optional<uint32_t> explicit_entry;

    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty() || line[0] != ':')
            continue;

        if (line.size() < 11)
            continue;

        auto hex2 = [&](size_t pos) -> uint8_t {
            return static_cast<uint8_t>(std::stoul(line.substr(pos, 2), nullptr, 16));
        };
        auto hex4 = [&](size_t pos) -> uint16_t {
            return static_cast<uint16_t>((hex2(pos) << 8) | hex2(pos + 2));
        };

        uint8_t byte_count = 0;
        uint16_t address = 0;
        uint8_t rec_type = 0;
        try
        {
            byte_count = hex2(1);
            address = hex4(3);
            rec_type = hex2(7);
        }
        catch (...)
        {
            continue;
        }

        if (line.size() < (11 + static_cast<size_t>(byte_count) * 2))
            continue;

        if (rec_type == 0x01)       // EOF
            break;

        if (rec_type == 0x00) // data
        {
            uint32_t base_addr = upper_base + static_cast<uint32_t>(address);
            if (byte_count > 0 && base_addr < lowest_data_addr)
                lowest_data_addr = base_addr;

            for (uint8_t i = 0; i < byte_count; ++i)
            {
                uint8_t byte = 0;
                try { byte = hex2(9 + static_cast<size_t>(i) * 2); }
                catch (...) { break; }
                size_t dest = static_cast<size_t>(base_addr + i);
                if (dest < mem.size())
                    mem[dest] = byte;
            }
        }
        else if (rec_type == 0x02 && byte_count >= 2) // extended segment addr
        {
            uint16_t seg = 0;
            try { seg = hex4(9); }
            catch (...) { continue; }
            upper_base = static_cast<uint32_t>(seg) << 4;
        }
        else if (rec_type == 0x04 && byte_count >= 2) // extended linear addr
        {
            uint16_t upper = 0;
            try { upper = hex4(9); }
            catch (...) { continue; }
            upper_base = static_cast<uint32_t>(upper) << 16;
        }
        else if (rec_type == 0x03 && byte_count >= 4) // start segment addr
        {
            uint16_t cs = 0, ip = 0;
            try
            {
                cs = hex4(9);
                ip = hex4(13);
            }
            catch (...) { continue; }
            explicit_entry = (static_cast<uint32_t>(cs) << 4) + ip;
        }
        else if (rec_type == 0x05 && byte_count >= 4) // start linear addr
        {
            uint32_t eip = 0;
            try
            {
                eip = (static_cast<uint32_t>(hex2(9)) << 24) |
                      (static_cast<uint32_t>(hex2(11)) << 16) |
                      (static_cast<uint32_t>(hex2(13)) << 8) |
                      static_cast<uint32_t>(hex2(15));
            }
            catch (...) { continue; }
            explicit_entry = eip;
        }
    }

    ihx_load_result result;
    if (explicit_entry.has_value())
    {
        result.entry = static_cast<uint16_t>(*explicit_entry & 0xFFFF);
        result.explicit_start = true;
    }
    else if (lowest_data_addr != 0xFFFFFFFF)
        result.entry = static_cast<uint16_t>(lowest_data_addr & 0xFFFF);

    return result;
}

} // anonymous namespace

namespace handlers {

class launch_handler : public dbg_handler {
public:
    using dbg_handler::dbg_handler;
    std::string command() const override { return "launch"; }

    std::string handle(const dap::request &req) override
    {
        auto r = dap::launch_request::from(req);
        auto start_override = parse_start_address_arg(r.arguments);

        z80ex_reset(ctx_.cpu());
        std::fill(ctx_.memory().begin(), ctx_.memory().end(), 0);
        ctx_.set_virtual_lst_source_reference(1);
        ctx_.clear_source_cache();
        ctx_.set_cdb_modules({});
        ctx_.set_source_roots({});
        ctx_.set_map_symbols({});
        ctx_.set_map_segments({});

        uint16_t entry = 0x0000;
        std::string entry_reason = "default 0x0000";
        if (start_override)
        {
            entry = *start_override;
            entry_reason = "from launch startAddress";
        }

        if (!r.program.empty())
        {
            std::string bin_path = r.program;
            std::string ext;
            try { ext = std::filesystem::path(bin_path).extension().string(); }
            catch (...) {}

            std::ifstream bin_file(bin_path, (ext == ".ihx" || ext == ".hex")
                                             ? std::ios::in : std::ios::binary);
            if (bin_file)
            {
                if (ext == ".ihx" || ext == ".hex")
                {
                    auto load_result = load_ihx(bin_file, ctx_.memory());
                    if (!start_override)
                    {
                        entry = load_result.entry;
                        entry_reason = load_result.explicit_start
                            ? "from IHX start address record"
                            : "from IHX lowest data address";
                    }
                }
                else
                {
                    // Raw binary load has no embedded metadata.
                    bin_file.read(reinterpret_cast<char *>(ctx_.memory().data()),
                                  ctx_.memory().size());
                }
                std::cerr << "[launch] Loaded program: " << bin_path << std::endl;
            }
            else
            {
                std::cerr << "[launch] ERROR: Cannot open program file: " << bin_path << std::endl;
            }

            namespace fs = std::filesystem;
            fs::path cdb_path = r.cdb_file.empty()
                ? fs::path(bin_path).replace_extension(".cdb")
                : fs::path(r.cdb_file);

            if (fs::exists(cdb_path))
            {
                sdcc::cdb_parser parser;
                auto modules = parser.parse(cdb_path.string());
                if (modules)
                {
                    std::cerr << "[launch] Loaded CDB: " << cdb_path.string()
                              << " (" << modules->size() << " modules";
                    size_t total_lines = 0;
                    for (auto &m : *modules) total_lines += m.lines.size();
                    std::cerr << ", " << total_lines << " line mappings)" << std::endl;
                    ctx_.set_cdb_modules(std::move(*modules));
                }
                else
                    std::cerr << "[launch] WARNING: Failed to parse CDB: " << cdb_path.string() << std::endl;
            }
            else
                std::cerr << "[launch] No CDB file found at: " << cdb_path.string() << std::endl;

            fs::path map_path = r.map_file.empty()
                ? fs::path(bin_path).replace_extension(".map")
                : fs::path(r.map_file);

            if (fs::exists(map_path))
            {
                sdcc::map_parser parser;
                auto map = parser.parse(map_path.string());
                if (map)
                {
                    ctx_.set_map_symbols(map->symbols);
                    ctx_.set_map_segments(map->segments);
                    std::cerr << "[launch] Loaded MAP: " << map_path.string()
                              << " (" << map->segments.size() << " segments, "
                              << map->symbols.size() << " symbols)" << std::endl;
                }
                else
                    std::cerr << "[launch] WARNING: Failed to parse MAP: " << map_path.string() << std::endl;
            }
            else
                std::cerr << "[launch] No MAP file found at: " << map_path.string() << std::endl;

            // Determine source root for resolving relative paths in CDB.
            std::string source_root = r.arguments.value("sourceRoot", "");
            ctx_.set_source_root(source_root.empty()
                ? fs::path(bin_path).parent_path().string()
                : source_root);

            std::vector<std::string> roots;
            for (const auto &key : {"sourceRoots", "includeRoots"}) {
                if (r.arguments.contains(key) && r.arguments[key].is_array())
                    for (const auto &item : r.arguments[key])
                        if (item.is_string())
                            roots.push_back(item.get<std::string>());
            }
            ctx_.set_source_roots(std::move(roots));
            ctx_.rebuild_source_breakpoint_addresses();

            std::string base;
            try { base = fs::path(bin_path).stem().string(); }
            catch (...) { base = "listing"; }
            ctx_.set_virtual_lst_path("/__virtual__/" + base + ".asm");
        // Build the full 64 KB disassembly listing once now so the source
        // view and breakpoints are ready before the first step.
        ctx_.build_full_listing();
        }
        else
        {
            ctx_.set_virtual_lst_path("/__virtual__/listing.asm");
        }

        z80ex_set_reg(ctx_.cpu(), regPC, entry);
        // Use a non-zero initial SP so signed SP comparisons in step-over/out
        // work correctly even when the program hasn't set its own stack yet.
        z80ex_set_reg(ctx_.cpu(), regSP, 0xFFFF);
        std::cerr << "[launch] Entry point: 0x"
                  << std::hex << entry << std::dec
                  << " (" << entry_reason << ")" << std::endl;

        ctx_.set_launched(true);
        ctx_.set_pending_entry_stop(true);

        dap::response resp(r.seq, r.command);
        resp.success(true).result({});

        return resp.str();
    }

private:
};

std::unique_ptr<dap::request_handler> make_launch(dbg &ctx)
{
    return make_handler<launch_handler>(ctx);
}

} // namespace handlers
