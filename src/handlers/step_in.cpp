// step_in.cpp — DAP "stepIn" request handler.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <dap/dap.h>
#include <dap/handler.h>
#include <dbg.h>

namespace handlers {

class step_in_handler : public dbg_handler {
public:
    using dbg_handler::dbg_handler;
    std::string command() const override { return "stepIn"; }

    std::string handle(const dap::request &req) override
    {
        auto r = dap::step_in_request::from(req);

        uint16_t start_pc  = z80ex_get_reg(ctx_.cpu(), regPC);
        auto     start_loc = ctx_.lookup_source(start_pc);

        if (start_loc && ctx_.has_cdb()) {
            // Source-level step-in: execute instructions until we land on a
            // C source line that is different from the one we started on.
            //
            // This skips:
            //  - call-setup code in the CALLER (instructions before the CALL
            //    opcode that load arguments — these have no C mapping)
            //  - function prologue in the CALLEE (PUSH instructions before
            //    the first C-mapped statement)
            //
            // The loop bound is generous because each iteration is a few
            // Z80 instructions (microseconds).
            for (int i = 0; i < 10000; ++i) {
                ctx_.step_instruction();
                uint16_t pc  = z80ex_get_reg(ctx_.cpu(), regPC);
                auto     loc = ctx_.lookup_source(pc);
                if (!loc) continue; // no C mapping here — keep going
                // Stop when we reach any C line different from the start.
                if (loc->file != start_loc->file || loc->line != start_loc->line)
                    break;
            }
        } else {
            // No C source at current PC — fall back to single instruction step.
            ctx_.step_instruction();
        }

        dap::response resp(r.seq, r.command);
        resp.success(true);

        nlohmann::json ev;
        ev["seq"]   = ctx_.next_event_seq();
        ev["type"]  = "event";
        ev["event"] = "stopped";
        ev["body"]  = {{"reason", "step"}, {"threadId", 1}, {"allThreadsStopped", true}};
        ctx_.queue_event(ev.dump());

        return resp.str();
    }
};

std::unique_ptr<dap::request_handler> make_step_in(dbg &ctx)
{
    return make_handler<step_in_handler>(ctx);
}

} // namespace handlers
