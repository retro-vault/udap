// disassemble.cpp — DAP "disassemble" request handler.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <sstream>
#include <iomanip>
#include <vector>

#include <dap/dap.h>
#include <dap/handler.h>
#include <dbg.h>

namespace handlers {

class disassemble_handler : public dap::request_handler {
public:
    disassemble_handler(dbg &ctx) : ctx_(ctx) {}
    std::string command() const override { return "disassemble"; }

    std::string handle(const dap::request &req) override
    {
        auto r = dap::disassemble_request::from(req);
        uint16_t base_addr = static_cast<uint16_t>(
            (r.memory_reference + r.offset) & 0xFFFF);
        auto &mem = ctx_.memory();
        uint16_t addr = base_addr;

        // Handle instruction_offset (can be negative).
        if (r.instruction_offset < 0)
        {
            int backstep = -r.instruction_offset;
            // Heuristic: scan forward from up to 4*backstep bytes before
            // the target. Z80 instructions are 1-4 bytes.
            int rewind = std::min((int)addr, backstep * 4);
            uint16_t scan = addr - rewind;
            std::vector<uint16_t> addrs;
            uint16_t pos = scan;
            while (pos <= addr + 3 && pos < mem.size())
            {
                addrs.push_back(pos);
                char buf[64];
                int t1 = 0, t2 = 0;
                int len = z80ex_dasm(buf, sizeof(buf), 0, &t1, &t2,
                                     dbg::dasm_readbyte_cb, pos, &mem);
                pos += (len > 0) ? len : 1;
            }
            // Find the entry for addr, then go back backstep instructions.
            int target_idx = static_cast<int>(addrs.size()) - 1;
            for (int i = 0; i < static_cast<int>(addrs.size()); ++i)
            {
                if (addrs[i] >= addr)
                {
                    target_idx = i;
                    break;
                }
            }
            int start_idx = std::max(0, target_idx - backstep);
            addr = addrs[start_idx];
        }
        else if (r.instruction_offset > 0)
        {
            for (int i = 0; i < r.instruction_offset && addr < mem.size(); ++i)
            {
                char buf[64];
                int t1 = 0, t2 = 0;
                int len = z80ex_dasm(buf, sizeof(buf), 0, &t1, &t2,
                                     dbg::dasm_readbyte_cb, addr, &mem);
                addr += (len > 0) ? len : 1;
            }
        }

        // Disassemble instruction_count instructions.
        nlohmann::json instructions = nlohmann::json::array();
        for (int i = 0; i < r.instruction_count && addr < mem.size(); ++i)
        {
            char dasm_buf[64];
            int ts1 = 0, ts2 = 0;
            int ilen = z80ex_dasm(dasm_buf, sizeof(dasm_buf), 0, &ts1, &ts2,
                                  dbg::dasm_readbyte_cb, addr, &mem);
            if (ilen <= 0)
                ilen = 1;

            // Build instruction bytes hex string.
            std::ostringstream bytes_oss;
            for (int j = 0; j < ilen && (addr + j) < mem.size(); ++j)
            {
                if (j > 0)
                    bytes_oss << " ";
                bytes_oss << std::uppercase << std::setw(2) << std::setfill('0')
                          << std::hex << static_cast<int>(mem[addr + j]);
            }

            nlohmann::json instr;
            instr["address"] = ctx_.format_hex(addr, 4);
            instr["instructionBytes"] = bytes_oss.str();
            instr["instruction"] = std::string(dasm_buf);

            // Exact symbol match for this address.
            auto sym = ctx_.lookup_symbol_exact(addr);
            if (sym)
                instr["symbol"] = *sym;

            // Source line mapping from CDB.
            auto loc = ctx_.lookup_source(addr);
            if (loc)
            {
                instr["location"] = {{"path", loc->file}};
                instr["line"] = loc->line;
            }

            instructions.push_back(instr);
            addr += ilen;
        }

        dap::response resp(r.seq, r.command);
        resp.success(true).result({{"instructions", instructions}});
        return resp.str();
    }

private:
    dbg &ctx_;
};

std::unique_ptr<dap::request_handler> make_disassemble(dbg &ctx)
{
    return std::make_unique<disassemble_handler>(ctx);
}

} // namespace handlers
