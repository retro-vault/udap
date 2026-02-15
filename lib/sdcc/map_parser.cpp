// map_parser.cpp
// Implementation of MAP parser for SDCC/ASxxxx linker output.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <regex>

#include <sdcc/map_parser.h>
#include <sdcc/util.h>

namespace sdcc {

std::optional<map_info> map_parser::parse(const std::string& path)
{
    auto lines = util::read_lines(path);
    if (!lines)
        return std::nullopt;

    data_ = {};

    // Example:
    // _CODE 00000100 000025FF = 9727. bytes (REL,CON)
    const std::regex segment_re(
        R"(^\s*([A-Za-z0-9_.\$]+)\s+((?:0[xX])?[0-9A-Fa-f]{4,8})\s+((?:0[xX])?[0-9A-Fa-f]{4,8}).*\(([^)]*)\)\s*$)");

    // Single-column format (older/larger MAP files):
    //   00000116  C$clock.c$18$0_0$36                clock
    const std::regex symbol_re(
        R"(^\s*((?:0[xX])?[0-9A-Fa-f]{4,8})\s+([^\s]+)(?:\s+([^\s]+))?\s*$)");

    // Multi-column format (newer sdld):
    //   0000802E  _cclear   |    0000803A  _cinit    |    00008044  _cputc
    // Each column: hex_address  symbol_name
    const std::regex column_sym_re(
        R"(\s*((?:0[xX])?[0-9A-Fa-f]{4,8})\s+([^\s|]+))");

    for (const auto& raw : *lines)
    {
        std::string line(raw);
        auto trimmed = util::trim(line);
        if (trimmed.empty())
            continue;

        if (auto seg = util::match(trimmed, segment_re))
        {
            try
            {
                segment s;
                s.name = (*seg)[0];
                s.address = static_cast<uint32_t>(std::stoul((*seg)[1], nullptr, 16));
                s.size = static_cast<uint32_t>(std::stoul((*seg)[2], nullptr, 16));
                s.attributes = (*seg)[3];
                data_.segments.push_back(std::move(s));
                continue;
            }
            catch (...) {}
        }

        // Try single-column format first.
        if (auto sym = util::match(trimmed, symbol_re))
        {
            // Skip table headings.
            if ((*sym)[0] == "Value" || (*sym)[1] == "Global")
                continue;

            try
            {
                symbol s;
                s.address = static_cast<uint32_t>(std::stoul((*sym)[0], nullptr, 16));
                s.name = (*sym)[1];
                s.area = (sym->size() > 2) ? (*sym)[2] : "";
                s.bank = 0;
                data_.symbols.push_back(std::move(s));
                continue;
            }
            catch (...) {}
        }

        // Try multi-column format (columns separated by '|').
        if (line.find('|') != std::string::npos)
        {
            auto it = std::sregex_iterator(line.begin(), line.end(), column_sym_re);
            auto end = std::sregex_iterator();
            for (; it != end; ++it)
            {
                try
                {
                    std::string addr_str = (*it)[1].str();
                    std::string name_str = (*it)[2].str();
                    if (addr_str == "Value" || name_str == "Global"
                        || name_str == "------")
                        continue;
                    symbol s;
                    s.address = static_cast<uint32_t>(
                        std::stoul(addr_str, nullptr, 16));
                    s.name = name_str;
                    s.bank = 0;
                    data_.symbols.push_back(std::move(s));
                }
                catch (...) {}
            }
        }
    }

    return data_;
}

} // namespace sdcc
