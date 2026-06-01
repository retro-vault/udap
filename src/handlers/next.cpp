// next.cpp — DAP "next" (step over) request handler.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <dap/dap.h>
#include <dap/handler.h>
#include <dbg.h>

namespace handlers {

namespace {

// True if op is the first byte of a Z80 CALL or RST instruction.
// RST is included because it also pushes a return address onto the stack.
bool is_z80_call(uint8_t op) {
    return op == 0xCD               // CALL nn
        || (op & 0xC7) == 0xC4     // CALL cc,nn  (NZ/Z/NC/C/PO/PE/P/M)
        || (op & 0xC7) == 0xC7;    // RST n
}

// True if op (and op2 for ED-prefixed opcodes) is a Z80 RET instruction.
bool is_z80_ret(uint8_t op, uint8_t op2) {
    return op == 0xC9                                       // RET
        || (op & 0xC7) == 0xC0                             // RET cc
        || (op == 0xED && (op2 == 0x4D || op2 == 0x45));  // RETI / RETN
}

} // namespace

class next_handler : public dbg_handler {
public:
    using dbg_handler::dbg_handler;
    std::string command() const override { return "next"; }

    std::string handle(const dap::request &req) override
    {
        auto r = dap::next_request::from(req);
        ctx_.push_history();
        uint16_t start_pc = z80ex_get_reg(ctx_.cpu(), regPC);
        auto start_loc    = ctx_.lookup_source_any(start_pc);

        auto step_over_loop = [&](auto should_stop) {
            int call_depth = 0;
            // Record SP before the first step.  When the actual SP rises back
            // to this level the callee has truly returned, regardless of whether
            // call_depth is still > 0 (which can happen when library functions
            // return via POP rr + JP (rr) instead of RET).
            int16_t start_sp =
                static_cast<int16_t>(z80ex_get_reg(ctx_.cpu(), regSP));

            for (int i = 0; i < 500000; ++i) {
                uint16_t pc  = z80ex_get_reg(ctx_.cpu(), regPC);
                uint8_t  op  = ctx_.memory()[pc];
                uint8_t  op2 = (pc + 1 < ctx_.memory().size())
                               ? ctx_.memory()[pc + 1] : 0;
                int16_t sp_before =
                    static_cast<int16_t>(z80ex_get_reg(ctx_.cpu(), regSP));

                ctx_.step_instruction();

                int16_t sp_after =
                    static_cast<int16_t>(z80ex_get_reg(ctx_.cpu(), regSP));
                int16_t sp_delta = sp_after - sp_before;

                if (sp_delta == -2 && is_z80_call(op))
                    call_depth++;
                else if (sp_delta == 2 && is_z80_ret(op, op2) && call_depth > 0)
                    call_depth--;

                // If SP has returned to or above the pre-step level, the callee
                // has exited — trust the stack over call_depth.
                if (sp_after >= start_sp)
                    call_depth = 0;

                if (call_depth > 0)
                    continue;

                if (should_stop(z80ex_get_reg(ctx_.cpu(), regPC)))
                    break;
            }
        };

        if (start_loc && start_loc->is_asm) {
            // Assembly step-over: stop at next different mapped location (asm or C).
            step_over_loop([&](uint16_t pc) {
                auto loc = ctx_.lookup_source_any(pc);
                return loc && (loc->file != start_loc->file || loc->line != start_loc->line);
            });
        } else if (start_loc) {
            // C source step-over: original behavior — only C entries, assembly invisible.
            step_over_loop([&](uint16_t pc) {
                auto loc = ctx_.lookup_source(pc);
                return loc && (loc->file != start_loc->file || loc->line != start_loc->line);
            });
        } else {
            // No source mapping — single instruction step.
            ctx_.step_instruction();
        }

        dap::response resp(r.seq, r.command);
        resp.success(true).result({{"allThreadsContinued", false}});

        nlohmann::json ev;
        ev["seq"]   = ctx_.next_event_seq();
        ev["type"]  = "event";
        ev["event"] = "stopped";
        ev["body"]  = {{"reason", "step"}, {"threadId", 1}, {"allThreadsStopped", true}};
        ctx_.queue_event(ev.dump());

        return resp.str();
    }
};

std::unique_ptr<dap::request_handler> make_next(dbg &ctx)
{
    return make_handler<next_handler>(ctx);
}

} // namespace handlers
