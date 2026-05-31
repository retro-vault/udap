// pause.cpp — DAP "pause" request handler.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <dap/dap.h>
#include <dap/handler.h>
#include <dbg.h>

namespace handlers {

class pause_handler : public dbg_handler {
public:
    using dbg_handler::dbg_handler;
    std::string command() const override { return "pause"; }

    std::string handle(const dap::request &req) override
    {
        ctx_.request_pause();
        dap::response resp(req.seq, req.command);
        return resp.success(true).str();
    }

private:
};

std::unique_ptr<dap::request_handler> make_pause(dbg &ctx)
{
    return make_handler<pause_handler>(ctx);
}

} // namespace handlers
