// next.cpp — DAP "next" (step over) request handler.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <dap/dap.h>
#include <dap/handler.h>
#include <dbg.h>

namespace handlers {

class next_handler : public dap::request_handler {
public:
    next_handler(dbg &ctx) : ctx_(ctx) {}
    std::string command() const override { return "next"; }

    std::string handle(const dap::request &req) override
    {
        auto r = dap::next_request::from(req);
        uint16_t start_pc = z80ex_get_reg(ctx_.cpu(), regPC);
        auto start_loc = ctx_.lookup_source(start_pc);

        // Source-level step when source mapping exists.
        // Track SP to implement step-over: when SP < start_sp we are
        // inside a called function and must keep stepping until we
        // return to (or above) the original stack level.
        if (start_loc)
        {
            uint16_t start_sp = z80ex_get_reg(ctx_.cpu(), regSP);
            for (int i = 0; i < 500000; ++i)
            {
                ctx_.step_instruction();
                uint16_t pc = z80ex_get_reg(ctx_.cpu(), regPC);
                uint16_t sp = z80ex_get_reg(ctx_.cpu(), regSP);

                // Still inside a called function — keep stepping.
                if (sp < start_sp)
                    continue;

                auto loc = ctx_.lookup_source(pc);
                if (!loc)
                    continue;
                if (loc->file != start_loc->file || loc->line != start_loc->line)
                    break;
            }
        }
        else
        {
            // Pure disassembly context: single instruction step.
            ctx_.step_instruction();
        }

        dap::response resp(r.seq, r.command);
        resp.success(true).result({{"allThreadsContinued", true}});

        std::thread([this]()
                    {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            nlohmann::json j;
            j["seq"] = ctx_.next_event_seq();
            j["type"] = "event";
            j["event"] = "stopped";
            j["body"] = {
                {"reason", "step"},
                {"threadId", 1},
                {"allThreadsStopped", true}};
            ctx_.send_event(j.dump()); })
            .detach();

        return resp.str();
    }

private:
    dbg &ctx_;
};

std::unique_ptr<dap::request_handler> make_next(dbg &ctx)
{
    return std::make_unique<next_handler>(ctx);
}

} // namespace handlers
