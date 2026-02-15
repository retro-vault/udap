// request.cpp
// Request parsing and conversion for Debug Adapter Protocol (DAP).
//
// This file implements the `dap::request` class and its subclasses,
// providing logic to parse generic JSON requests and convert them into
// specific typed request structures like `initialize_request`, 
// `launch_request`, etc., based on the command field. This allows 
// type-safe dispatch in the DAP handler.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <stdexcept>

#include <dap/dap.h>

namespace dap
{
    // --- Static helper functions. ----------------------------------

    // Read common request members.
    template <typename T>
    static T base_copy(const request &req)
    {
        T r;
        r.seq = req.seq;
        r.type = req.type;
        r.command = req.command;
        r.arguments = req.arguments;
        return r;
    }

    // Helper function to parse JSON safely.
    static json parse_json_safely(const std::string &json_text)
    {
        try
        {
            return json::parse(json_text);
        }
        catch (const std::exception &ex)
        {
            throw std::runtime_error(std::string("Failed to parse JSON: ") + 
                ex.what());
        }
    }


    // --- Request(s) member functions. ------------------------------
    request request::parse(const std::string &json_text)
    {
        json j = parse_json_safely(json_text);
        request base;
        base.seq = j.value("seq", 0);
        base.type = j.value("type", "");
        base.command = j.value("command", "");
        base.arguments = j.value("arguments", json::object());
        return base_copy<request>(base); // Apply conversion logic if any.
    }


    // --- DAP request conversions -----------------------------------
    initialize_request initialize_request::from(const request &req)
    {
        initialize_request r = base_copy<initialize_request>(req);
        r.adapter_id = req.arguments.value("adapterID", "");
        r.client_id = req.arguments.value("clientID", "");
        r.client_name = req.arguments.value("clientName", "");
        r.locale = req.arguments.value("locale", "");
        return r;
    }

    launch_request launch_request::from(const request &req)
    {
        launch_request r = base_copy<launch_request>(req);
        r.no_debug = req.arguments.value("noDebug", false);
        r.program = req.arguments.value("program", "");
        return r;
    }

    attach_request attach_request::from(const request &req)
    {
        return base_copy<attach_request>(req);
    }

    set_breakpoints_request set_breakpoints_request::from(const request &req)
    {
        set_breakpoints_request r = base_copy<set_breakpoints_request>(req);
        r.source = req.arguments.value("source", json::object());
        r.breakpoints = req.arguments.value("breakpoints", json::array());
        return r;
    }

    configuration_done_request configuration_done_request::from(const request &req)
    {
        return base_copy<configuration_done_request>(req);
    }

    threads_request threads_request::from(const request &req)
    {
        return base_copy<threads_request>(req);
    }

    stack_trace_request stack_trace_request::from(const request &req)
    {
        stack_trace_request r = base_copy<stack_trace_request>(req);
        r.thread_id = req.arguments.value("threadId", 0);
        r.start_frame = req.arguments.value("startFrame", 0);
        r.levels = req.arguments.value("levels", 0);
        return r;
    }

    scopes_request scopes_request::from(const request &req)
    {
        scopes_request r = base_copy<scopes_request>(req);
        r.frame_id = req.arguments.value("frameId", 0);
        return r;
    }

    variables_request variables_request::from(const request &req)
    {
        variables_request r = base_copy<variables_request>(req);
        r.variables_reference = req.arguments.value("variablesReference", 0);
        return r;
    }

    continue_request continue_request::from(const request &req)
    {
        continue_request r = base_copy<continue_request>(req);
        r.thread_id = req.arguments.value("threadId", 0);
        return r;
    }

    source_request source_request::from(const request &req)
    {
        source_request r = base_copy<source_request>(req);

        if (req.arguments.contains("sourceReference"))
            r.source_reference = req.arguments["sourceReference"].get<int>();
        else if (req.arguments.contains("source") &&
                 req.arguments["source"].contains("sourceReference"))
            r.source_reference = req.arguments["source"]["sourceReference"].get<int>();

        return r;
    }

    read_memory_request read_memory_request::from(const request &req)
    {
        read_memory_request r = base_copy<read_memory_request>(req);
        if (req.arguments.contains("memoryReference"))
        {
            auto &ref = req.arguments["memoryReference"];
            if (ref.is_string())
                r.memory_reference = static_cast<int>(std::stoul(ref.get<std::string>(), nullptr, 0));
            else
                r.memory_reference = ref.get<int>();
        }
        r.offset = req.arguments.value("offset", 0);
        r.count = req.arguments.value("count", 0);
        return r;
    }

    next_request next_request::from(const request &req)
    {
        next_request r = base_copy<next_request>(req);
        r.thread_id = req.arguments.value("threadId", 0);
        return r;
    }

    step_in_request step_in_request::from(const request &req)
    {
        step_in_request r = base_copy<step_in_request>(req);
        r.thread_id = req.arguments.value("threadId", 0);
        r.granularity = req.arguments.value("granularity", "");
        return r;
    }

    step_out_request step_out_request::from(const request &req)
    {
        step_out_request r = base_copy<step_out_request>(req);
        r.thread_id = req.arguments.value("threadId", 0);
        r.granularity = req.arguments.value("granularity", "");
        return r;
    }

    set_instruction_breakpoints_request set_instruction_breakpoints_request::from(const request &req)
    {
        set_instruction_breakpoints_request r = base_copy<set_instruction_breakpoints_request>(req);
        r.breakpoints = req.arguments.value("breakpoints", std::vector<json>{});
        return r;
    }

    breakpoint_locations_request breakpoint_locations_request::from(const request &req)
    {
        breakpoint_locations_request r = base_copy<breakpoint_locations_request>(req);
        r.source = req.arguments.value("source", json::object());
        r.line = req.arguments.value("line", 0);
        r.end_line = req.arguments.value("endLine", 0);
        if (r.end_line == 0)
            r.end_line = r.line;
        return r;
    }

    set_function_breakpoints_request set_function_breakpoints_request::from(const request &req)
    {
        set_function_breakpoints_request r = base_copy<set_function_breakpoints_request>(req);
        r.breakpoints = req.arguments.value("breakpoints", std::vector<json>{});
        return r;
    }

    disassemble_request disassemble_request::from(const request &req)
    {
        disassemble_request r = base_copy<disassemble_request>(req);
        if (req.arguments.contains("memoryReference"))
        {
            std::string ref = req.arguments["memoryReference"].get<std::string>();
            r.memory_reference = static_cast<int>(std::stoul(ref, nullptr, 0));
        }
        r.offset = req.arguments.value("offset", 0);
        r.instruction_offset = req.arguments.value("instructionOffset", 0);
        r.instruction_count = req.arguments.value("instructionCount", 0);
        return r;
    }

    evaluate_request evaluate_request::from(const request &req)
    {
        evaluate_request r = base_copy<evaluate_request>(req);
        r.expression = req.arguments.value("expression", "");
        r.context = req.arguments.value("context", "");
        r.frame_id = req.arguments.value("frameId", 0);
        return r;
    }

} // namespace dap
