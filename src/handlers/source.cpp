// source.cpp — DAP "source" request handler.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <fstream>
#include <sstream>
#include <regex>
#include <dap/dap.h>
#include <dap/handler.h>
#include <dbg.h>

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

class source_handler : public dbg_handler {
public:
    using dbg_handler::dbg_handler;
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

        // The full 64 KB listing is built once at launch and served verbatim
        // on every request.  The sourceReference stays fixed; only the line
        // number in the stack frame changes to follow the PC.
        if (!ctx_.has_full_listing())
            ctx_.build_full_listing(); // safety fallback (should already be built)

        dap::response resp(r.seq, r.command);
        resp.success(true).result({{"content",  ctx_.full_listing_content()},
                                   {"mimeType", "text/x-asm"}});
        return resp.str();
    }

private:
};

std::unique_ptr<dap::request_handler> make_source(dbg &ctx)
{
    return make_handler<source_handler>(ctx);
}

} // namespace handlers
