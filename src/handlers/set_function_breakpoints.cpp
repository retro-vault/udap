// set_function_breakpoints.cpp — DAP "setFunctionBreakpoints" request handler.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <dap/dap.h>
#include <dap/handler.h>
#include <dbg.h>

namespace handlers {

class set_function_breakpoints_handler : public dap::request_handler {
public:
    set_function_breakpoints_handler(dbg &ctx) : ctx_(ctx) {}
    std::string command() const override { return "setFunctionBreakpoints"; }

    std::string handle(const dap::request &req) override
    {
        auto r = dap::set_function_breakpoints_request::from(req);

        ctx_.function_breakpoints().clear();
        nlohmann::json results = nlohmann::json::array();

        for (const auto &bp : r.breakpoints)
        {
            std::string name = bp.value("name", "");
            auto addr = ctx_.lookup_function_address(name);
            if (addr)
            {
                ctx_.function_breakpoints().push_back(*addr);
                results.push_back({
                    {"verified", true},
                    {"instructionReference", ctx_.format_hex(*addr, 4)}});
            }
            else
            {
                results.push_back({
                    {"verified", false},
                    {"message", "Function not found: " + name}});
            }
        }

        dap::response resp(r.seq, r.command);
        resp.success(true).result({{"breakpoints", results}});
        return resp.str();
    }

private:
    dbg &ctx_;
};

std::unique_ptr<dap::request_handler> make_set_function_breakpoints(dbg &ctx)
{
    return std::make_unique<set_function_breakpoints_handler>(ctx);
}

} // namespace handlers
