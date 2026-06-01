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
        ctx_.push_history();
        uint16_t start_pc  = z80ex_get_reg(ctx_.cpu(), regPC);
        auto     start_loc = ctx_.lookup_source_any(start_pc);

        if (start_loc && start_loc->is_asm) {
            // Assembly step-in: stop at next different mapped location (asm or C).
            for (int i = 0; i < 10000; ++i) {
                ctx_.step_instruction();
                uint16_t pc  = z80ex_get_reg(ctx_.cpu(), regPC);
                auto     loc = ctx_.lookup_source_any(pc);
                if (!loc) continue;
                if (loc->file != start_loc->file || loc->line != start_loc->line)
                    break;
            }
        } else if (start_loc && ctx_.has_cdb()) {
            // C source step-in: original behavior (C entries only) plus CALL detection
            // to enter assembly functions when F11 is pressed on a CALL instruction.
            for (int i = 0; i < 10000; ++i) {
                uint16_t pc        = z80ex_get_reg(ctx_.cpu(), regPC);
                uint8_t  op        = ctx_.memory()[pc];
                int16_t  sp_before = static_cast<int16_t>(z80ex_get_reg(ctx_.cpu(), regSP));

                ctx_.step_instruction();

                int16_t  sp_after = static_cast<int16_t>(z80ex_get_reg(ctx_.cpu(), regSP));
                uint16_t new_pc   = z80ex_get_reg(ctx_.cpu(), regPC);

                // Stop at a different C line (original behavior).
                auto loc = ctx_.lookup_source(new_pc);
                if (loc && (loc->file != start_loc->file || loc->line != start_loc->line))
                    break;

                // Also stop when a CALL/RST lands us inside an assembly function.
                if (!loc) {
                    bool entered_call = (sp_after - sp_before == -2)
                        && (op == 0xCD || (op & 0xC7) == 0xC4 || (op & 0xC7) == 0xC7);
                    if (entered_call && ctx_.lookup_source_any(new_pc))
                        break;
                }
            }
        } else {
            // No source mapping — single instruction step.
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
