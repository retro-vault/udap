// continue.cpp — DAP "continue" request handler.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <dap/dap.h>
#include <dap/handler.h>
#include <dbg.h>

namespace handlers {

class continue_handler : public dap::request_handler {
public:
    continue_handler(dbg &ctx) : ctx_(ctx) {}
    std::string command() const override { return "continue"; }

    std::string handle(const dap::request &req) override
    {
        auto r = dap::continue_request::from(req);
        ctx_.clear_pause();

        bool hit_breakpoint = false;
        bool hit_halt = false;
        for (int step = 0; step < 10000000; ++step)
        {
            // Step first so we don't re-trigger the breakpoint
            // we're currently stopped at.
            ctx_.step_instruction();
            if (!ctx_.launched())
                break;

            uint16_t pc = z80ex_get_reg(ctx_.cpu(), regPC);

            // Check source breakpoints (addresses resolved from CDB).
            if (std::find(ctx_.breakpoints().begin(),
                          ctx_.breakpoints().end(),
                          pc) != ctx_.breakpoints().end())
            {
                hit_breakpoint = true;
                break;
            }

            // Check instruction breakpoints (raw addresses).
            if (std::find(ctx_.instruction_breakpoints().begin(),
                          ctx_.instruction_breakpoints().end(),
                          pc) != ctx_.instruction_breakpoints().end())
            {
                hit_breakpoint = true;
                break;
            }

            // Check function breakpoints.
            if (std::find(ctx_.function_breakpoints().begin(),
                          ctx_.function_breakpoints().end(),
                          pc) != ctx_.function_breakpoints().end())
            {
                hit_breakpoint = true;
                break;
            }

            // Check if pause was requested.
            if (ctx_.pause_requested())
            {
                ctx_.clear_pause();
                break;
            }

            // Stop on HALT instruction.
            if (ctx_.memory()[pc] == 0x76)
            {
                hit_halt = true;
                break;
            }
        }

        dap::response resp(r.seq, r.command);
        resp.success(true).result({{"allThreadsContinued", true}});

        // Always send a stopped event so VSCode never gets stuck
        // thinking the program is still running.
        std::string reason = hit_breakpoint ? "breakpoint" : "pause";
        std::thread([this, reason]()
                    {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            nlohmann::json j;
            j["seq"] = ctx_.next_event_seq();
            j["type"] = "event";
            j["event"] = "stopped";
            j["body"] = {
                {"reason", reason},
                {"threadId", 1},
                {"allThreadsStopped", true}};
            ctx_.send_event(j.dump()); })
            .detach();

        return resp.str();
    }

private:
    dbg &ctx_;
};

std::unique_ptr<dap::request_handler> make_continue(dbg &ctx)
{
    return std::make_unique<continue_handler>(ctx);
}

} // namespace handlers
