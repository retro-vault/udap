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
        uint16_t start_pc = z80ex_get_reg(ctx_.cpu(), regPC);
        auto start_loc    = ctx_.lookup_source(start_pc);

        if (start_loc) {
            // Source-level step-over using opcode-aware call-depth tracking.
            //
            // The old SP-only heuristic ("if sp < start_sp we're in a nested
            // call") was wrong: a PUSH in a function prologue also lowers SP,
            // causing the loop to skip the entire function body until the
            // matching POPs restore SP.
            //
            // Fix: pair opcode inspection with SP delta.
            //   CALL taken → opcode is CALL/RST type AND sp decreased by 2
            //   RET  taken → opcode is RET  type      AND sp increased by 2
            // PUSH/POP have the same SP delta but different opcodes, so they
            // don't affect call_depth.
            int call_depth = 0;

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

                if (call_depth > 0)
                    continue; // still inside a called function

                uint16_t new_pc = z80ex_get_reg(ctx_.cpu(), regPC);
                auto loc = ctx_.lookup_source(new_pc);
                if (!loc) continue;
                if (loc->file != start_loc->file || loc->line != start_loc->line)
                    break;
            }
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
