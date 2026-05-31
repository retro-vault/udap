// variables.cpp — DAP "variables" request handler.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <dap/dap.h>
#include <dap/handler.h>
#include <dbg.h>

namespace handlers {

namespace {

nlohmann::json make_reg(const char *name, uint16_t val, int width, bool with_mem)
{
    nlohmann::json v;
    v["name"]               = name;
    v["value"]              = dbg::format_hex(val, width);
    v["variablesReference"] = 0;
    if (with_mem)
        v["memoryReference"] = dbg::format_hex(val, 4);
    return v;
}

} // namespace

class variables_handler : public dbg_handler {
public:
    using dbg_handler::dbg_handler;
    std::string command() const override { return "variables"; }

    std::string handle(const dap::request &req) override
    {
        auto r = dap::variables_request::from(req);
        nlohmann::json vars = nlohmann::json::array();

        if (r.variables_reference == var_ref::cpu_group) {
            vars.push_back({{"name", "CPU"}, {"value", ""},
                            {"variablesReference", var_ref::cpu_registers}});

        } else if (r.variables_reference == var_ref::cpu_registers) {
            auto *c = ctx_.cpu();
            vars.push_back(make_reg("AF", z80ex_get_reg(c, regAF), 4, false));
            vars.push_back(make_reg("BC", z80ex_get_reg(c, regBC), 4, true));
            vars.push_back(make_reg("DE", z80ex_get_reg(c, regDE), 4, true));
            vars.push_back(make_reg("HL", z80ex_get_reg(c, regHL), 4, true));
            vars.push_back(make_reg("IX", z80ex_get_reg(c, regIX), 4, true));
            vars.push_back(make_reg("IY", z80ex_get_reg(c, regIY), 4, true));
            vars.push_back(make_reg("SP", z80ex_get_reg(c, regSP), 4, true));
            vars.push_back(make_reg("PC", z80ex_get_reg(c, regPC), 4, true));
            vars.push_back(make_reg("R",  z80ex_get_reg(c, regR),  2, false));
            vars.push_back(make_reg("I",  z80ex_get_reg(c, regI),  2, false));
            // F is the low byte of AF.
            vars.push_back(make_reg("F",
                static_cast<uint16_t>(z80ex_get_reg(c, regAF) & 0xFF), 2, false));

        } else if (r.variables_reference == var_ref::c_locals) {
            uint16_t pc = z80ex_get_reg(ctx_.cpu(), regPC);
            auto *fn = ctx_.lookup_function_at(pc);
            if (fn) {
                for (const auto &sym : fn->local_symbols) {
                    vars.push_back({
                        {"name",               sym.name},
                        {"value",              ctx_.c_variable_value(sym)},
                        {"type",               sym.type_info},
                        {"variablesReference", 0}});
                }
            }

        } else if (r.variables_reference == var_ref::c_globals) {
            for (const auto &mod : ctx_.cdb_modules()) {
                for (const auto &sym : mod.global_symbols) {
                    // Skip function declarations and library stubs.
                    // "DF" in the type_info means "derived function" —
                    // it appears in declarations like putchar, printf, main,
                    // and internal static function references.  These have no
                    // memory address to read and would only clutter the view.
                    if (sym.type_info.find("DF") != std::string::npos) continue;
                    vars.push_back({
                        {"name",               sym.name},
                        {"value",              ctx_.c_variable_value(sym)},
                        {"type",               sym.type_info},
                        {"variablesReference", 0}});
                }
            }

        } else if (r.variables_reference == var_ref::map_group) {
            vars.push_back({{"name", "Segments"},
                            {"value", std::to_string(ctx_.map_segments().size())},
                            {"variablesReference", var_ref::map_segments}});
            vars.push_back({{"name", "Symbols"},
                            {"value", std::to_string(ctx_.map_symbols().size())},
                            {"variablesReference", var_ref::map_symbols}});

        } else if (r.variables_reference == var_ref::map_segments) {
            for (const auto &seg : ctx_.map_segments()) {
                uint16_t addr = static_cast<uint16_t>(seg.address & 0xFFFF);
                vars.push_back({
                    {"name",  seg.name},
                    {"value", "addr=" + dbg::format_hex(addr, 4) +
                              ", size=" + dbg::format_hex(
                                  static_cast<uint16_t>(seg.size & 0xFFFF), 4) +
                              ", " + seg.attributes},
                    {"variablesReference", 0},
                    {"memoryReference",    dbg::format_hex(addr, 4)}});
            }

        } else if (r.variables_reference == var_ref::map_symbols) {
            for (const auto &sym : ctx_.map_symbols()) {
                uint16_t addr = static_cast<uint16_t>(sym.address & 0xFFFF);
                vars.push_back({
                    {"name",               sym.name},
                    {"value",              dbg::format_hex(addr, 4)},
                    {"variablesReference", 0},
                    {"memoryReference",    dbg::format_hex(addr, 4)}});
            }
        }

        dap::response resp(r.seq, r.command);
        resp.success(true).result({{"variables", vars}});
        return resp.str();
    }

private:
};

std::unique_ptr<dap::request_handler> make_variables(dbg &ctx)
{
    return make_handler<variables_handler>(ctx);
}

} // namespace handlers
