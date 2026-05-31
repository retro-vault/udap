// emulator.cpp
// Z80 CPU setup/teardown and background emulation for the DAP server.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <dbg.h>

static uint8_t memread_cb(Z80EX_CONTEXT *, uint16_t addr,
    int /*m1_state*/, void *user_data)
{
    return static_cast<dbg *>(user_data)->memory()[addr];
}

static void memwrite_cb(Z80EX_CONTEXT *, uint16_t addr,
    uint8_t value, void *user_data)
{
    static_cast<dbg *>(user_data)->memory()[addr] = value;
}

static uint8_t portread_cb(Z80EX_CONTEXT *, uint16_t, void *) { return 0xFF; }
static void    portwrite_cb(Z80EX_CONTEXT *, uint16_t, uint8_t, void *) {}
static uint8_t intread_cb(Z80EX_CONTEXT *, void *) { return 0; }

dbg::dbg()
    : cpu_(nullptr), memory_(0x10000, 0),
      event_seq_(1), launched_(false)
{
    cpu_ = z80ex_create(
        memread_cb, this,
        memwrite_cb, this,
        portread_cb, this,
        portwrite_cb, this,
        intread_cb, this);
}

dbg::~dbg()
{
    // Signal the emulation thread to stop, then wait for it to exit
    // so the thread never accesses this object after destruction.
    stop_emulation();
    if (cpu_) z80ex_destroy(cpu_);
}

// Execute one complete Z80 instruction, treating block instructions as atomic.
//
// The inner loop handles ED/DD/FD/CB prefix bytes — z80ex_last_op_type()
// returns non-zero while a prefix was consumed rather than a full opcode.
//
// The outer loop handles Z80 block instructions (LDIR, LDDR, CPIR, CPDR,
// INIR, INDR, OTIR, OTDR — all encoded as ED B0..B3 / ED B8..BB).  On the
// real Z80 these instructions subtract 2 from PC on each iteration that
// doesn't yet satisfy their termination condition, so z80ex keeps PC at the
// instruction address until BC reaches zero (or the match/port condition is
// met).  Without the outer loop, callers would see BC decremented by one per
// call — requiring as many steps as the initial BC value.  The outer loop
// runs until PC finally leaves the instruction, making it appear atomic.
int dbg::step_instruction()
{
    uint16_t start_pc = static_cast<uint16_t>(z80ex_get_reg(cpu_, regPC));

    // Detect block instructions: first byte 0xED, second byte in B0-B3 or B8-BB.
    uint8_t b0 = memory_[start_pc];
    uint8_t b1 = (start_pc + 1u < memory_.size()) ? memory_[start_pc + 1] : 0u;
    bool is_block = (b0 == 0xED) &&
                    ((b1 >= 0xB0 && b1 <= 0xB3) || (b1 >= 0xB8 && b1 <= 0xBB));

    int total = 0;
    do {
        // Inner loop: consume prefix bytes until a full opcode completes.
        do {
            total += z80ex_step(cpu_);
        } while (z80ex_last_op_type(cpu_) != 0);
        // Outer loop: for block instructions keep going until PC advances.
    } while (is_block &&
             static_cast<uint16_t>(z80ex_get_reg(cpu_, regPC)) == start_pc);

    return total;
}
