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

class breakpoint_locations_handler : public dbg_handler {
public:
    using dbg_handler::dbg_handler;
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

        for (const auto &mod : ctx_.cdb_modules()) {
            for (const auto &ln : mod.lines) {
                if (ln.line < r.line || ln.line > r.end_line) continue;
                if (fs::path(ln.file).filename().string() == query_name)
                    valid_lines.insert(ln.line);
            }
        }

        // Fallback: MAP symbols via dbg::map_symbol_to_source (no duplication).
        for (const auto &sym : ctx_.map_symbols()) {
            auto loc = ctx_.map_symbol_to_source(sym);
            if (!loc || loc->line < r.line || loc->line > r.end_line) continue;
            if (fs::path(loc->file).filename().string() == query_name)
                valid_lines.insert(loc->line);
        }

        nlohmann::json locations = nlohmann::json::array();
        for (int line : valid_lines)
            locations.push_back({{"line", line}});

        dap::response resp(r.seq, r.command);
        resp.success(true).result({{"breakpoints", locations}});
        return resp.str();
    }

private:
};

std::unique_ptr<dap::request_handler> make_breakpoint_locations(dbg &ctx)
{
    return make_handler<breakpoint_locations_handler>(ctx);
}

} // namespace handlers
