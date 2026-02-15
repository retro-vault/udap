// configuration_done.cpp — DAP "configurationDone" request handler.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <dap/dap.h>
#include <dap/handler.h>
#include <dbg.h>

namespace handlers {

class configuration_done_handler : public dap::request_handler {
public:
    configuration_done_handler(dbg &ctx) : ctx_(ctx) {}
    std::string command() const override { return "configurationDone"; }

    std::string handle(const dap::request &req) override
    {
        auto r = dap::configuration_done_request::from(req);

        dap::response resp(r.seq, r.command);
        std::string response = resp.success(true).result({}).str();

        if (ctx_.launched() && ctx_.pending_entry_stop())
        {
            ctx_.set_pending_entry_stop(false);
            std::thread([this]()
                        {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                nlohmann::json stopped_event;
                stopped_event["seq"] = ctx_.next_event_seq();
                stopped_event["type"] = "event";
                stopped_event["event"] = "stopped";
                stopped_event["body"] = {
                    {"reason", "step"},
                    {"threadId", 1},
                    {"allThreadsStopped", true}};
                ctx_.send_event(stopped_event.dump()); })
                .detach();
        }

        return response;
    }

private:
    dbg &ctx_;
};

std::unique_ptr<dap::request_handler> make_configuration_done(dbg &ctx)
{
    return std::make_unique<configuration_done_handler>(ctx);
}

} // namespace handlers
