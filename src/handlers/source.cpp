// source.cpp — DAP "source" request handler.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <dap/dap.h>
#include <dap/handler.h>
#include <dbg.h>
#include <regex>

namespace handlers {

namespace {

std::string replace_hex_operands(
    const std::string &input,
    const std::regex &pattern,
    int capture_group,
    dbg &ctx)
{
    std::string out;
    std::sregex_iterator it(input.begin(), input.end(), pattern);
    std::sregex_iterator end;

    size_t last = 0;
    for (; it != end; ++it)
    {
        const auto &m = *it;
        out.append(input.substr(last, static_cast<size_t>(m.position()) - last));

        uint16_t addr = 0;
        bool parsed = false;
        try
        {
            addr = static_cast<uint16_t>(std::stoul(m.str(capture_group), nullptr, 16));
            parsed = true;
        }
        catch (...) {}

        if (parsed)
        {
            // Only replace on exact symbol hits — no fuzzy/nearest matching.
            auto sym = ctx.lookup_symbol_exact(addr);
            if (sym)
            {
                // Keep immediate marker if present in original operand.
                if (!m.str().empty() && m.str()[0] == '#')
                    out.append("#");
                out.append(*sym);
            }
            else
                out.append(m.str());
        }
        else
            out.append(m.str());

        last = static_cast<size_t>(m.position() + m.length());
    }
    out.append(input.substr(last));
    return out;
}

// Check if a Z80 mnemonic is an 8-bit immediate load (LD r8,#nn).
bool is_ld_r8_imm(const std::string &line)
{
    // z80ex disassembles these as e.g. "LD A,#xx", "LD B,#xx", "LD (HL),#xx"
    static const std::regex re(
        R"(LD\s+(?:[A-E]|H|L|IXH|IXL|IYH|IYL|\([A-Z]+(?:[+-][0-9]+)?\))\s*,\s*#)",
        std::regex::icase);
    return std::regex_search(line, re);
}

std::string symbolize_disassembly(const std::string &line, dbg &ctx)
{
    // Don't symbolize 8-bit immediates — they're never addresses.
    if (is_ld_r8_imm(line))
        return line;

    // Cover the most common operand styles emitted by the disassembler:
    // 0x1234, $1234, 1234h
    static const std::regex r_0x(R"(0x([0-9A-Fa-f]{1,4}))");
    static const std::regex r_dollar(R"(\$([0-9A-Fa-f]{1,4}))");
    static const std::regex r_hsuffix(R"(\b([0-9A-Fa-f]{1,4})[Hh]\b)");
    static const std::regex r_hash(R"(#([0-9A-Fa-f]{1,4}))");

    std::string s = replace_hex_operands(line, r_0x, 1, ctx);
    s = replace_hex_operands(s, r_dollar, 1, ctx);
    s = replace_hex_operands(s, r_hsuffix, 1, ctx);
    s = replace_hex_operands(s, r_hash, 1, ctx);
    return s;
}

} // namespace

class source_handler : public dap::request_handler {
public:
    source_handler(dbg &ctx) : ctx_(ctx) {}
    std::string command() const override { return "source"; }

    std::string handle(const dap::request &req) override
    {
        auto r = dap::source_request::from(req);

        if (r.source_reference > 0)
        {
            auto cached = ctx_.source_by_reference(r.source_reference);
            if (cached)
            {
                dap::response resp(r.seq, r.command);
                resp.success(true)
                    .result({{"content", cached->content},
                             {"mimeType", cached->mime_type}});
                return resp.str();
            }
        }

        // sourceReference 0 means "use the path on disk".
        // Unknown non-zero source references fall back to virtual disassembly.
        if (r.source_reference == 0)
        {
            std::string path;
            if (req.arguments.contains("source") &&
                req.arguments["source"].contains("path"))
                path = req.arguments["source"]["path"].get<std::string>();

            if (!path.empty())
            {
                std::ifstream ifs(path);
                if (ifs)
                {
                    std::ostringstream content;
                    content << ifs.rdbuf();
                    dap::response resp(r.seq, r.command);
                    resp.success(true)
                        .result({{"content", content.str()},
                                 {"mimeType", "text/x-c"}});
                    return resp.str();
                }
            }

            dap::response resp(r.seq, r.command);
            resp.success(false).message("Unknown sourceReference");
            return resp.str();
        }

        std::ostringstream oss;
        char dasm_buf[64];
        uint32_t addr = z80ex_get_reg(ctx_.cpu(), regPC);

        auto &mem = ctx_.memory();
        for (int i = 0; i < 256 && addr < mem.size();)
        {
            int ts1 = 0, ts2 = 0;
            int ilen = z80ex_dasm(
                dasm_buf, sizeof(dasm_buf), 0, &ts1, &ts2,
                dbg::dasm_readbyte_cb, addr, &mem);

            oss << "      " << std::uppercase << std::setfill('0')
                << std::setw(6) << std::hex << addr << " ";

            int opcode_chars = 0;
            for (int j = 0; j < ilen && (addr + j) < mem.size(); ++j)
            {
                oss << std::setw(2) << std::setfill('0') << std::hex
                    << (int)mem[addr + j] << " ";
                opcode_chars += 3;
            }
            for (; opcode_chars < 8; ++opcode_chars)
                oss << " ";

            oss << std::string(26 - (6 + 6 + 1 + opcode_chars), ' ');
            oss << "[" << std::right << std::setw(2) << std::dec << ts1 << "]";
            oss << "   ";
            oss << symbolize_disassembly(dasm_buf, ctx_) << "\n";

            addr += (ilen > 0) ? ilen : 1;
            ++i;
        }

        dap::response resp(r.seq, r.command);
        resp.success(true)
            .result({{"content", oss.str()},
                     {"mimeType", "text/x-asm"}});
        return resp.str();
    }

private:
    dbg &ctx_;
};

std::unique_ptr<dap::request_handler> make_source(dbg &ctx)
{
    return std::make_unique<source_handler>(ctx);
}

} // namespace handlers
