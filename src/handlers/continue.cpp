// continue.cpp — DAP "continue" request handler.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <dap/dap.h>
#include <dap/handler.h>
#include <dbg.h>

namespace handlers {

class continue_handler : public dbg_handler {
public:
    using dbg_handler::dbg_handler;
    std::string command() const override { return "continue"; }

    std::string handle(const dap::request &req) override
    {
        auto r = dap::continue_request::from(req);
        ctx_.clear_pause();
        // start_emulation() runs on a background thread and posts a
        // "stopped" event via queue_event() when it halts.  The I/O
        // thread is free to receive further messages (e.g. "pause").
        ctx_.start_emulation();

        dap::response resp(r.seq, r.command);
        return resp.success(true).result({{"allThreadsContinued", true}}).str();
    }

private:
};

std::unique_ptr<dap::request_handler> make_continue(dbg &ctx)
{
    return make_handler<continue_handler>(ctx);
}

} // namespace handlers
