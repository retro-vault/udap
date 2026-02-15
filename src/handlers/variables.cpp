// variables.cpp — DAP "variables" request handler.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <dap/dap.h>
#include <dap/handler.h>
#include <dbg.h>

namespace handlers {

class variables_handler : public dap::request_handler {
public:
    variables_handler(dbg &ctx) : ctx_(ctx) {}
    std::string command() const override { return "variables"; }

    std::string handle(const dap::request &req) override
    {
        auto r = dap::variables_request::from(req);
        nlohmann::json vars = nlohmann::json::array();

        if (r.variables_reference == 100)
        {
            vars.push_back({
                {"name", "CPU"},
                {"value", ""},
                {"variablesReference", 101},
            });
        }
        else if (r.variables_reference == 101)
        {
            // 16-bit register pairs that can be used as pointers get memoryReference.
#define Z80REG(rname, regid, width)                                         \
    {                                                                       \
        nlohmann::json v;                                                   \
        v["name"] = #rname;                                                 \
        uint16_t val = z80ex_get_reg(ctx_.cpu(), regid);                    \
        v["value"] = ctx_.format_hex(val, width);                           \
        v["variablesReference"] = 0;                                        \
        v["memoryReference"] = ctx_.format_hex(val, 4);                     \
        vars.push_back(v);                                                  \
    }
#define Z80REG_NOMEM(rname, regid, width)                                   \
    {                                                                       \
        nlohmann::json v;                                                   \
        v["name"] = #rname;                                                 \
        v["value"] = ctx_.format_hex(z80ex_get_reg(ctx_.cpu(), regid), width); \
        v["variablesReference"] = 0;                                        \
        vars.push_back(v);                                                  \
    }
            Z80REG_NOMEM(AF, regAF, 4)
            Z80REG(BC, regBC, 4)
            Z80REG(DE, regDE, 4)
            Z80REG(HL, regHL, 4)
            Z80REG(IX, regIX, 4)
            Z80REG(IY, regIY, 4)
            Z80REG(SP, regSP, 4)
            Z80REG(PC, regPC, 4)
            Z80REG_NOMEM(R, regR, 2)
            Z80REG_NOMEM(I, regI, 2)
#undef Z80REG
#undef Z80REG_NOMEM

            vars.push_back({{"name", "F"},
                            {"value", ctx_.format_hex(
                                z80ex_get_reg(ctx_.cpu(), regAF) & 0xFF, 2)},
                            {"variablesReference", 0}});
        }
        else if (r.variables_reference == 200)
        {
            vars.push_back({
                {"name", "Segments"},
                {"value", std::to_string(ctx_.map_segments().size())},
                {"variablesReference", 201},
            });
            vars.push_back({
                {"name", "Symbols"},
                {"value", std::to_string(ctx_.map_symbols().size())},
                {"variablesReference", 202},
            });
        }
        else if (r.variables_reference == 201)
        {
            for (const auto &seg : ctx_.map_segments())
            {
                uint16_t addr = static_cast<uint16_t>(seg.address & 0xFFFF);
                vars.push_back({
                    {"name", seg.name},
                    {"value",
                     "addr=" + ctx_.format_hex(addr, 4) +
                         ", size=" + ctx_.format_hex(static_cast<uint16_t>(seg.size & 0xFFFF), 4) +
                         ", " + seg.attributes},
                    {"variablesReference", 0},
                    {"memoryReference", ctx_.format_hex(addr, 4)},
                });
            }
        }
        else if (r.variables_reference == 202)
        {
            for (const auto &sym : ctx_.map_symbols())
            {
                uint16_t addr = static_cast<uint16_t>(sym.address & 0xFFFF);
                vars.push_back({
                    {"name", sym.name},
                    {"value", ctx_.format_hex(addr, 4)},
                    {"variablesReference", 0},
                    {"memoryReference", ctx_.format_hex(addr, 4)},
                });
            }
        }

        dap::response resp(r.seq, r.command);
        resp.success(true).result({{"variables", vars}});
        return resp.str();
    }

private:
    dbg &ctx_;
};

std::unique_ptr<dap::request_handler> make_variables(dbg &ctx)
{
    return std::make_unique<variables_handler>(ctx);
}

} // namespace handlers
