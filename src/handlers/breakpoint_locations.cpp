// breakpoint_locations.cpp — DAP "breakpointLocations" request handler.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <set>
#include <filesystem>

#include <dap/dap.h>
#include <dap/handler.h>
#include <dbg.h>

namespace handlers {

class breakpoint_locations_handler : public dap::request_handler {
public:
    breakpoint_locations_handler(dbg &ctx) : ctx_(ctx) {}
    std::string command() const override { return "breakpointLocations"; }

    std::string handle(const dap::request &req) override
    {
        namespace fs = std::filesystem;
        auto r = dap::breakpoint_locations_request::from(req);

        std::string source_path;
        if (r.source.contains("path"))
            source_path = r.source["path"].get<std::string>();
        else if (r.source.contains("name"))
            source_path = r.source["name"].get<std::string>();

        std::string query_name = fs::path(source_path).filename().string();

        std::set<int> valid_lines;

        // Search CDB modules for lines with addresses in the requested range.
        for (const auto &mod : ctx_.cdb_modules())
        {
            for (const auto &ln : mod.lines)
            {
                if (ln.line < r.line || ln.line > r.end_line)
                    continue;
                std::string cdb_name = fs::path(ln.file).filename().string();
                if (cdb_name == query_name)
                    valid_lines.insert(ln.line);
            }
        }

        // Fallback: MAP symbols with C$file$line$... pattern.
        for (const auto &sym : ctx_.map_symbols())
        {
            if (sym.name.size() < 4 || sym.name[0] != 'C' || sym.name[1] != '$')
                continue;
            size_t p1 = sym.name.find('$', 2);
            if (p1 == std::string::npos)
                continue;
            size_t p2 = sym.name.find('$', p1 + 1);
            if (p2 == std::string::npos)
                continue;
            std::string file = sym.name.substr(2, p1 - 2);
            int line = 0;
            try
            {
                line = std::stoi(sym.name.substr(p1 + 1, p2 - p1 - 1));
            }
            catch (...)
            {
                continue;
            }
            if (line < r.line || line > r.end_line)
                continue;
            if (fs::path(file).filename().string() == query_name)
                valid_lines.insert(line);
        }

        nlohmann::json locations = nlohmann::json::array();
        for (int line : valid_lines)
            locations.push_back({{"line", line}});

        dap::response resp(r.seq, r.command);
        resp.success(true).result({{"breakpoints", locations}});
        return resp.str();
    }

private:
    dbg &ctx_;
};

std::unique_ptr<dap::request_handler> make_breakpoint_locations(dbg &ctx)
{
    return std::make_unique<breakpoint_locations_handler>(ctx);
}

} // namespace handlers
