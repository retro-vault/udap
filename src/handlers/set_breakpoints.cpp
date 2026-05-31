// set_breakpoints.cpp — DAP "setBreakpoints" request handler.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <dap/dap.h>
#include <dap/handler.h>
#include <dbg.h>

namespace handlers {

class set_breakpoints_handler : public dbg_handler {
public:
    using dbg_handler::dbg_handler;
    std::string command() const override { return "setBreakpoints"; }

    std::string handle(const dap::request &req) override
    {
        auto r = dap::set_breakpoints_request::from(req);

        // Virtual assembly listing breakpoints.
        // VSCode sends sourceReference > 0 (the fixed virtual listing ref)
        // with no path, or with name = "z80.s" but still no disk path.
        // Check: sourceReference matches the virtual listing ref.
        if (r.source_reference > 0 &&
            r.source_reference == ctx_.virtual_lst_source_reference()) {
            nlohmann::json bps = nlohmann::json::array();
            std::vector<uint16_t> addrs;
            const auto &line_addrs = ctx_.full_listing_addrs();

            for (int line : r.lines) {
                int idx = line - 1;
                if (idx >= 0 && static_cast<size_t>(idx) < line_addrs.size()) {
                    uint16_t addr = line_addrs[static_cast<size_t>(idx)];
                    addrs.push_back(addr);
                    bps.push_back({{"verified",             true},
                                   {"line",                 line},
                                   {"instructionReference", dbg::format_hex(addr, 4)}});
                } else {
                    bps.push_back({{"verified", false}, {"line", line},
                                   {"message", "Line out of range"}});
                }
            }
            ctx_.set_asm_breakpoints(std::move(addrs));

            dap::response resp(r.seq, r.command);
            resp.success(true).result({{"breakpoints", bps}});
            return resp.str();
        }

        // Regular C source breakpoints (CDB/MAP driven).
        ctx_.set_source_breakpoints_for_file(r.source_path, r.lines);
        auto bps = ctx_.resolve_source_breakpoints_for_file(r.source_path);
        ctx_.rebuild_source_breakpoint_addresses();

        dap::response resp(r.seq, r.command);
        resp.success(true).result({{"breakpoints", bps}});
        return resp.str();
    }

private:
};

std::unique_ptr<dap::request_handler> make_set_breakpoints(dbg &ctx)
{
    return make_handler<set_breakpoints_handler>(ctx);
}

} // namespace handlers
