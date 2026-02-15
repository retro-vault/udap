// pause.cpp — DAP "pause" request handler.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <dap/dap.h>
#include <dap/handler.h>
#include <dbg.h>

namespace handlers {

class pause_handler : public dap::request_handler {
public:
    pause_handler(dbg &ctx) : ctx_(ctx) {}
    std::string command() const override { return "pause"; }

    std::string handle(const dap::request &req) override
    {
        ctx_.request_pause();
        dap::response resp(req.seq, req.command);
        return resp.success(true).str();
    }

private:
    dbg &ctx_;
};

std::unique_ptr<dap::request_handler> make_pause(dbg &ctx)
{
    return std::make_unique<pause_handler>(ctx);
}

} // namespace handlers
