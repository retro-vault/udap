// step_out.cpp — DAP "stepOut" request handler.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <dap/dap.h>
#include <dap/handler.h>
#include <dbg.h>

namespace handlers {

class step_out_handler : public dap::request_handler {
public:
    step_out_handler(dbg &ctx) : ctx_(ctx) {}
    std::string command() const override { return "stepOut"; }

    std::string handle(const dap::request &req) override
    {
        auto r = dap::step_out_request::from(req);
        ctx_.step_instruction();

        dap::response resp(r.seq, r.command);
        resp.success(true);

        // Send stopped event AFTER the response (DAP protocol requirement).
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

std::unique_ptr<dap::request_handler> make_step_out(dbg &ctx)
{
    return std::make_unique<step_out_handler>(ctx);
}

} // namespace handlers
