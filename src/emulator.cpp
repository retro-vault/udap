// emulator.cpp
// Z80 CPU emulation setup and teardown for DAP server.
//
// This file defines the memory and I/O callbacks required by the z80ex
// library and implements the `dbg` constructor and destructor to initialize
// and destroy the Z80 CPU context for debugging purposes.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <dbg.h>

static uint8_t memread_cb(Z80EX_CONTEXT *, uint16_t addr,
    int m1_state, void *user_data)
{
    auto *dbg_ptr = static_cast<dbg *>(user_data);
    return dbg_ptr->memory()[addr];
}

static void memwrite_cb(Z80EX_CONTEXT *, uint16_t addr,
    uint8_t value, void *user_data)
{
    auto *dbg_ptr = static_cast<dbg *>(user_data);
    dbg_ptr->memory()[addr] = value;
}

static uint8_t portread_cb(Z80EX_CONTEXT *, uint16_t, void *)
{
    return 0xFF;
}

static void portwrite_cb(Z80EX_CONTEXT *, uint16_t, uint8_t, void *) {}

static uint8_t intread_cb(Z80EX_CONTEXT *, void *)
{
    return 0;
}

dbg::dbg()
    : cpu_(nullptr), memory_(0x10000, 0),
      breakpoints_(), event_seq_(1), launched_(false)
{
    cpu_ = z80ex_create(
        memread_cb, this,
        memwrite_cb, this,
        portread_cb, this,
        portwrite_cb, this,
        intread_cb, this);
}

int dbg::step_instruction()
{
    int total_tstates = 0;
    do {
        total_tstates += z80ex_step(cpu_);
    } while (z80ex_last_op_type(cpu_) != 0);
    return total_tstates;
}

dbg::~dbg()
{
    if (cpu_)
        z80ex_destroy(cpu_);
}
