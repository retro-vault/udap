// step_out.cpp — DAP "stepOut" request handler.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <dap/dap.h>
#include <dap/handler.h>
#include <dbg.h>

namespace handlers {

class step_out_handler : public dbg_handler {
public:
    using dbg_handler::dbg_handler;
    std::string command() const override { return "stepOut"; }

    std::string handle(const dap::request &req) override
    {
        auto r = dap::step_out_request::from(req);

        // Step until the current function returns.
        // entry_sp is the SP *inside* the callee (2 below the caller's SP,
        // because CALL pushed the return address).  After the matching RET,
        // SP will be entry_sp + 2 — strictly greater than entry_sp.
        //
        // Using ">=" was wrong: it also fires on the last POP in the function
        // epilogue (which restores SP to exactly entry_sp) before RET executes,
        // so step-out stopped one instruction too early, still inside the callee.
        int16_t entry_sp = static_cast<int16_t>(z80ex_get_reg(ctx_.cpu(), regSP));

        for (int i = 0; i < 500000; ++i) {
            ctx_.step_instruction();
            int16_t sp = static_cast<int16_t>(z80ex_get_reg(ctx_.cpu(), regSP));
            if (sp > entry_sp) break; // RET executed — we're back in the caller
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

private:
};

std::unique_ptr<dap::request_handler> make_step_out(dbg &ctx)
{
    return make_handler<step_out_handler>(ctx);
}

} // namespace handlers
