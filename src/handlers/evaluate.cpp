// evaluate.cpp — DAP "evaluate" request handler.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <algorithm>
#include <cctype>

#include <dap/dap.h>
#include <dap/handler.h>
#include <dbg.h>

namespace handlers {

class evaluate_handler : public dbg_handler {
public:
    using dbg_handler::dbg_handler;
    std::string command() const override { return "evaluate"; }

    std::string handle(const dap::request &req) override
    {
        auto r = dap::evaluate_request::from(req);
        const std::string &expr = r.expression;

        std::string upper = expr;
        std::ranges::transform(upper, upper.begin(), ::toupper);

        // Z80 register lookup.
        struct reg_entry { const char *name; Z80_REG_T id; int width; };
        static constexpr reg_entry regs[] = {
            {"AF", regAF, 4}, {"BC", regBC, 4}, {"DE", regDE, 4},
            {"HL", regHL, 4}, {"IX", regIX, 4}, {"IY", regIY, 4},
            {"SP", regSP, 4}, {"PC", regPC, 4},
            {"A",  regAF, 2}, {"F",  regAF, 2},
            {"B",  regBC, 2}, {"C",  regBC, 2},
            {"D",  regDE, 2}, {"E",  regDE, 2},
            {"H",  regHL, 2}, {"L",  regHL, 2},
            {"R",  regR,  2}, {"I",  regI,  2},
        };

        for (const auto &reg : regs) {
            if (upper != reg.name) continue;
            uint16_t val = z80ex_get_reg(ctx_.cpu(), reg.id);
            if (reg.width == 2 && std::strlen(reg.name) == 1) {
                char ch = reg.name[0];
                if (ch == 'A' || ch == 'B' || ch == 'D' || ch == 'H')
                    val = (val >> 8) & 0xFF;
                else
                    val = val & 0xFF;
            }
            dap::response resp(r.seq, r.command);
            nlohmann::json body;
            body["result"]              = dbg::format_hex(val, reg.width);
            body["variablesReference"]  = 0;
            if (reg.width == 4)
                body["memoryReference"] = dbg::format_hex(val, 4);
            return resp.success(true).result(body).str();
        }

        // MAP symbol lookup.
        for (const auto &sym : ctx_.map_symbols()) {
            if (sym.name == expr || sym.name == "_" + expr) {
                uint16_t addr = static_cast<uint16_t>(sym.address & 0xFFFF);
                nlohmann::json body;
                body["result"]             = dbg::format_hex(addr, 4);
                body["variablesReference"] = 0;
                body["memoryReference"]    = dbg::format_hex(addr, 4);
                dap::response resp(r.seq, r.command);
                return resp.success(true).result(body).str();
            }
        }

        // Hex address literal: 0xNNNN or $NNNN — show byte at that address.
        try {
            uint16_t val = 0;
            if (expr.size() > 2 && expr[0] == '0' && (expr[1] == 'x' || expr[1] == 'X'))
                val = static_cast<uint16_t>(std::stoul(expr, nullptr, 0));
            else if (expr.size() > 1 && expr[0] == '$')
                val = static_cast<uint16_t>(std::stoul(expr.substr(1), nullptr, 16));
            else {
                dap::response resp(r.seq, r.command);
                return resp.success(false).message("Cannot evaluate: " + expr).str();
            }
            uint8_t byte = ctx_.memory()[val];
            nlohmann::json body;
            body["result"]             = dbg::format_hex(byte, 2);
            body["variablesReference"] = 0;
            body["memoryReference"]    = dbg::format_hex(val, 4);
            dap::response resp(r.seq, r.command);
            return resp.success(true).result(body).str();
        } catch (...) {}

        dap::response resp(r.seq, r.command);
        return resp.success(false).message("Cannot evaluate: " + expr).str();
    }

private:
};

std::unique_ptr<dap::request_handler> make_evaluate(dbg &ctx)
{
    return make_handler<evaluate_handler>(ctx);
}

} // namespace handlers
