// scopes.cpp — DAP "scopes" request handler.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <dap/dap.h>
#include <dap/handler.h>
#include <dbg.h>

namespace handlers {

class scopes_handler : public dbg_handler {
public:
    using dbg_handler::dbg_handler;
    std::string command() const override { return "scopes"; }

    std::string handle(const dap::request &req) override
    {
        auto r = dap::scopes_request::from(req);
        nlohmann::json scopes = nlohmann::json::array();

        // Always show CPU registers.
        scopes.push_back({
            {"name",               "Registers"},
            {"variablesReference", var_ref::cpu_group},
            {"presentationHint",   "registers"},
            {"expensive",          false}});

        // C locals — shown when CDB data is loaded and we're in a known function.
        if (ctx_.has_cdb()) {
            uint16_t pc = z80ex_get_reg(ctx_.cpu(), regPC);
            auto *fn = ctx_.lookup_function_at(pc);
            if (fn && !fn->local_symbols.empty()) {
                scopes.push_back({
                    {"name",               "Locals"},
                    {"variablesReference", var_ref::c_locals},
                    {"presentationHint",   "locals"},
                    {"expensive",          false}});
            }
        }

        // C globals — shown when CDB data is loaded.
        if (ctx_.has_cdb()) {
            scopes.push_back({
                {"name",               "Globals"},
                {"variablesReference", var_ref::c_globals},
                {"presentationHint",   "locals"},
                {"expensive",          false}});
        }

        // Raw MAP symbols/segments (always present when MAP is loaded).
        if (ctx_.has_map()) {
            scopes.push_back({
                {"name",               "MAP"},
                {"variablesReference", var_ref::map_group},
                {"presentationHint",   "locals"},
                {"expensive",          false}});
        }

        dap::response resp(r.seq, r.command);
        resp.success(true).result({{"scopes", scopes}});
        return resp.str();
    }
};

std::unique_ptr<dap::request_handler> make_scopes(dbg &ctx)
{
    return make_handler<scopes_handler>(ctx);
}

} // namespace handlers
