// disconnect.cpp — DAP "disconnect" request handler.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <dap/dap.h>
#include <dap/handler.h>
#include <dbg.h>

namespace handlers {

class disconnect_handler : public dbg_handler {
public:
    using dbg_handler::dbg_handler;
    std::string command() const override { return "disconnect"; }

    std::string handle(const dap::request &req) override
    {
        ctx_.set_launched(false);
        ctx_.stop_emulation(); // ensures background thread exits cleanly
        dap::response resp(req.seq, req.command);
        return resp.success(true).result({}).str();
    }

private:
};

std::unique_ptr<dap::request_handler> make_disconnect(dbg &ctx)
{
    return make_handler<disconnect_handler>(ctx);
}

} // namespace handlers
