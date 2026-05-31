// configuration_done.cpp — DAP "configurationDone" request handler.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <dap/dap.h>
#include <dap/handler.h>
#include <dbg.h>

namespace handlers {

class configuration_done_handler : public dbg_handler {
public:
    using dbg_handler::dbg_handler;
    std::string command() const override { return "configurationDone"; }

    std::string handle(const dap::request &req) override
    {
        auto r = dap::configuration_done_request::from(req);

        dap::response resp(r.seq, r.command);
        std::string response = resp.success(true).result({}).str();

        if (ctx_.launched() && ctx_.pending_entry_stop()) {
            ctx_.set_pending_entry_stop(false);

            nlohmann::json ev;
            ev["seq"]   = ctx_.next_event_seq();
            ev["type"]  = "event";
            ev["event"] = "stopped";
            ev["body"]  = {
                {"reason",           "step"},
                {"threadId",         1},
                {"allThreadsStopped", true}};
            ctx_.queue_event(ev.dump());
        }

        return response;
    }

private:
};

std::unique_ptr<dap::request_handler> make_configuration_done(dbg &ctx)
{
    return make_handler<configuration_done_handler>(ctx);
}

} // namespace handlers
