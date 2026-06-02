// z80_target.h
// Z80/SDCC debug target — implements dap::target for the Z80 emulator.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#pragma once

#include <dap/target.h>
#include <dbg.h>

class z80_target : public dap::target {
public:
    z80_target();
    ~z80_target() override = default;

    bool launch(const dap::launch_args &args) override;
    void disconnect()                          override;

    void resume()    override;
    void pause()     override;
    void step()      override;
    void step_in()   override;
    void step_out()  override;
    void step_back() override;

    std::vector<uint8_t>              read_memory(uint16_t addr, int count)        const override;
    std::vector<dap::frame_info>      get_stack()                                  const override;
    std::vector<dap::scope_info>      get_scopes()                                 const override;
    std::vector<dap::variable_info>   get_variables(const std::string &scope)      const override;

    std::vector<dap::breakpoint_info> set_source_breakpoints(
        const std::string &path, int source_reference,
        const std::vector<int> &lines)                                                   override;
    std::vector<dap::breakpoint_info> set_function_breakpoints(
        const std::vector<std::string> &names)                                           override;
    std::vector<dap::breakpoint_info> set_instruction_breakpoints(
        const std::vector<std::string> &instruction_references)                          override;

    std::optional<dap::source_info>   get_source(const std::string &path)          const override;

    std::vector<dap::disasm_info>     disassemble(
        int memory_reference, int offset,
        int instruction_offset, int instruction_count)                             const override;

    dap::eval_info                    evaluate(const std::string &expr)            const override;

    std::vector<dap::bp_location_info>   get_breakpoint_locations(
        const std::string &path, int line, int end_line)                           const override;
    std::vector<dap::loaded_source_info> get_loaded_sources()                      const override;

private:
    mutable dbg dbg_;
};
