// initialize.cpp — DAP "initialize" request handler.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <dap/dap.h>
#include <dap/handler.h>
#include <dbg.h>

namespace handlers {

class initialize_handler : public dbg_handler {
public:
    using dbg_handler::dbg_handler;
    std::string command() const override { return "initialize"; }

    std::string handle(const dap::request &req) override
    {
        auto r = dap::initialize_request::from(req);

        // Queue the "initialized" event; it will be delivered by
        // send_event_direct() (mutex-protected) immediately after the
        // response is written, so ordering is guaranteed without sleeps.
        nlohmann::json ev;
        ev["seq"]   = ctx_.next_event_seq();
        ev["type"]  = "event";
        ev["event"] = "initialized";
        ev["body"]  = nlohmann::json::object();
        ctx_.queue_event(ev.dump());

        dap::response resp(r.seq, r.command);
        return resp
            .success(true)
            .result({{"supportsConfigurationDoneRequest",    true},
                     {"supportsBreakpointLocationsRequest",  true},
                     {"supportsInstructionBreakpoints",      true},
                     {"supportsLoadedSourcesRequest",        true},
                     {"supportsDisassembleRequest",          true},
                     {"supportsFunctionBreakpoints",         true},
                     {"supportsStepBack",                    false},
                     {"supportsRestartFrame",                false},
                     {"supportsEvaluateForHovers",           true},
                     {"supportsSetVariable",                 false},
                     {"supportsTerminateDebuggee",           false},
                     {"supportsMemoryReferences",            true},
                     {"supportsReadMemoryRequest",           true}})
            .str();
    }

private:
};

std::unique_ptr<dap::request_handler> make_initialize(dbg &ctx)
{
    return make_handler<initialize_handler>(ctx);
}

} // namespace handlers
