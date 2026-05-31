// stack_trace.cpp — DAP "stackTrace" request handler.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <dap/dap.h>
#include <dap/handler.h>
#include <dbg.h>

namespace handlers {

class stack_trace_handler : public dbg_handler {
public:
    using dbg_handler::dbg_handler;
    std::string command() const override { return "stackTrace"; }

    std::string handle(const dap::request &req) override
    {
        auto r = dap::stack_trace_request::from(req);
        uint16_t pc = z80ex_get_reg(ctx_.cpu(), regPC);

        nlohmann::json frame;

        // Try C source mapping from CDB first.
        auto src = ctx_.has_cdb() ? ctx_.lookup_source(pc) : std::nullopt;
        if (src)
        {
            std::string name = std::filesystem::path(src->file).filename().string();
            nlohmann::json source = {
                {"name", name},
                {"presentationHint", "normal"}};
            // If the file exists on disk, always use path so VSCode opens
            // the real file (editable, breakpoints work).  Only fall back
            // to sourceReference for files that can't be found.
            if (std::filesystem::exists(src->file))
            {
                source["path"] = src->file;
                source["sourceReference"] = 0;
            }
            else
            {
                int source_ref = ctx_.ensure_source_reference(src->file, "text/x-c");
                if (source_ref > 0)
                    source["sourceReference"] = source_ref;
            }

            frame = {
                {"id", 1},
                {"name", name + ":" + std::to_string(src->line)},
                {"memoryReference", ctx_.format_hex(pc, 4)},
                {"instructionReference", ctx_.format_hex(pc, 4)},
                {"source", source},
                {"line", src->line},
                {"column", 1}};
        }
        else
        {
            // Fall back to the full 64 KB virtual disassembly listing.
            // The sourceReference is fixed for the whole session; VSCode
            // reuses the cached content and only moves the cursor to pc_line.
            int  source_ref = ctx_.virtual_lst_source_reference();
            int  pc_line    = ctx_.full_listing_line_for_addr(pc);
            auto sym        = ctx_.lookup_symbol(pc);

            frame = {
                {"id", 1},
                {"name", sym ? *sym : ctx_.format_hex(pc, 4)},
                {"memoryReference", ctx_.format_hex(pc, 4)},
                {"instructionReference", ctx_.format_hex(pc, 4)},
                {"source",
                 {{"name", "z80.s"},
                  {"sourceReference", source_ref},
                  {"presentationHint", "normal"},
                  {"mimeType", "text/x-asm"}}},
                {"line",   pc_line},
                {"column", 1}};
        }

        dap::response resp(r.seq, r.command);
        resp.success(true).result({{"stackFrames", {frame}},
                                   {"totalFrames", 1}});
        return resp.str();
    }

private:
};

std::unique_ptr<dap::request_handler> make_stack_trace(dbg &ctx)
{
    return make_handler<stack_trace_handler>(ctx);
}

} // namespace handlers
