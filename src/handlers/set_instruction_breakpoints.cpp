// set_instruction_breakpoints.cpp — DAP "setInstructionBreakpoints" handler.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <dap/dap.h>
#include <dap/handler.h>
#include <dbg.h>

namespace handlers {

class set_instruction_breakpoints_handler : public dbg_handler {
public:
    using dbg_handler::dbg_handler;
    std::string command() const override { return "setInstructionBreakpoints"; }

    std::string handle(const dap::request &req) override
    {
        auto r = dap::set_instruction_breakpoints_request::from(req);

        ctx_.instruction_breakpoints().clear();
        for (const auto &bp : r.breakpoints) {
            if (!bp.contains("instructionReference")) continue;
            try {
                uint16_t addr = static_cast<uint16_t>(
                    std::stoul(bp["instructionReference"].get<std::string>(), nullptr, 0));
                ctx_.instruction_breakpoints().push_back(addr);
            } catch (...) {}
        }
        ctx_.rebuild_all_breakpoints();

        std::vector<nlohmann::json> breakpoints;
        for (uint16_t addr : ctx_.instruction_breakpoints())
            breakpoints.push_back({{"verified", true},
                                   {"instructionReference", dbg::format_hex(addr, 4)}});

        dap::response resp(r.seq, r.command);
        resp.success(true).result({{"breakpoints", breakpoints}});
        return resp.str();
    }

private:
};

std::unique_ptr<dap::request_handler> make_set_instruction_breakpoints(dbg &ctx)
{
    return make_handler<set_instruction_breakpoints_handler>(ctx);
}

} // namespace handlers
