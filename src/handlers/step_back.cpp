// step_back.cpp — DAP "stepBack" request handler.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <dap/dap.h>
#include <dap/handler.h>
#include <dbg.h>

namespace handlers {

class step_back_handler : public dbg_handler {
public:
    using dbg_handler::dbg_handler;
    std::string command() const override { return "stepBack"; }

    std::string handle(const dap::request &req) override
    {
        auto r = dap::step_back_request::from(req);

        ctx_.pop_history();

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

std::unique_ptr<dap::request_handler> make_step_back(dbg &ctx)
{
    return make_handler<step_back_handler>(ctx);
}

} // namespace handlers
