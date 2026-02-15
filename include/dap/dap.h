// dap.h
// Core types and server logic for the Debug Adapter Protocol (DAP).
//
// This file defines the request and response structures, along with the
// `dap` server class used for handling DAP messages. It provides
// request-specific subclasses for parsing incoming JSON commands and
// dispatching them to registered handlers.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <mutex>
#include <sstream>
#include <iostream>

#include <nlohmann/json.hpp>

namespace dap
{
    using json = nlohmann::json;

    // Generic request wrapper. Used as the base for all DAP request types.
    struct request
    {
        int seq = 0;
        std::string type;               // Usually "request".
        std::string command;            // DAP command.
        json arguments;                 // Arbitrary arguments.

        static request parse(const std::string &json_text);
        virtual ~request() = default;
    };

    // Abstract base for DAP request handlers (chain of responsibility).
    class request_handler {
    public:
        virtual ~request_handler() = default;
        virtual std::string command() const = 0;
        virtual std::string handle(const request& req) = 0;
    };

    // Initialize request. Sent by the client at startup.
    struct initialize_request : public request
    {
        std::string adapter_id;
        std::string client_id;
        std::string client_name;
        std::string locale;

        static initialize_request from(const request &req);
    };

    // Launch request. Start the debug session.
    struct launch_request : public request
    {
        bool no_debug = false;
        std::string program;

        static launch_request from(const request &req);
    };

    // Attach request. Attach to a running process.
    struct attach_request : public request
    {
        static attach_request from(const request &req);
    };

    // Set source breakpoints. Set file-based breakpoints.
    struct set_breakpoints_request : public request
    {
        json source;
        json breakpoints;

        static set_breakpoints_request from(const request &req);
    };

    // Configuration done. Finalize setup.
    struct configuration_done_request : public request
    {
        static configuration_done_request from(const request &req);
    };

    // Request thread list. Retrieve thread info.
    struct threads_request : public request
    {
        static threads_request from(const request &req);
    };

    // Request stack trace. Get current call stack.
    struct stack_trace_request : public request
    {
        int thread_id = 0;
        int start_frame = 0;
        int levels = 0;

        static stack_trace_request from(const request &req);
    };

    // Request scopes. List variable scopes.
    struct scopes_request : public request
    {
        int frame_id = 0;

        static scopes_request from(const request &req);
    };

    // Request variables. Query variable contents.
    struct variables_request : public request
    {
        int variables_reference = 0;

        static variables_request from(const request &req);
    };

    // Continue request. Resume execution.
    struct continue_request : public request
    {
        int thread_id = 0;

        static continue_request from(const request &req);
    };

    // Request source contents. Retrieve source file contents.
    struct source_request : public request
    {
        int source_reference = 0;

        static source_request from(const request &req);
    };

    // Read memory from emulated system. Read raw memory.
    struct read_memory_request : public request
    {
        int memory_reference = 0;
        int offset = 0;
        int count = 0;

        static read_memory_request from(const request &req);
    };

    // Disassemble memory. Disassemble memory contents.
    struct disassemble_request : public request
    {
        int memory_reference = 0;
        int offset = 0;
        int instruction_offset = 0;
        int instruction_count = 0;

        static disassemble_request from(const request &req);
    };

    // Breakpoint locations. Find valid breakpoint positions.
    struct breakpoint_locations_request : public request
    {
        json source;
        int line = 0;
        int end_line = 0;

        static breakpoint_locations_request from(const request &req);
    };

    // Set function breakpoints. Break on function entry.
    struct set_function_breakpoints_request : public request
    {
        std::vector<json> breakpoints;

        static set_function_breakpoints_request from(const request &req);
    };

    // Set instruction breakpoints. Break on address.
    struct set_instruction_breakpoints_request : public request
    {
        std::vector<json> breakpoints;

        static set_instruction_breakpoints_request from(const request &req);
    };

    // Step over. Step over one instruction.
    struct next_request : public request
    {
        int thread_id = 0;

        static next_request from(const request &req);
    };

    // Step into. Step into next call.
    struct step_in_request : public request
    {
        int thread_id = 0;
        std::string granularity;

        static step_in_request from(const request &req);
    };

    // Step out. Step out of current function.
    struct step_out_request : public request
    {
        int thread_id = 0;
        std::string granularity;

        static step_out_request from(const request &req);
    };

    // Evaluate expression. Used by Watch panel and debug console.
    struct evaluate_request : public request
    {
        std::string expression;
        std::string context; // "watch", "repl", "hover"
        int frame_id = 0;

        static evaluate_request from(const request &req);
    };

    // Response generator.
    class response
    {
    public:
        response(int request_seq, const std::string &command);

        response &success(bool ok = true);
        response &message(const std::string &msg);
        response &result(const json &result_data);
        std::string str() const;

    private:
        json _json;                     // Final response JSON.
    };

    // DAP dispatcher with chain of responsibility pattern.
    // Handlers are walked in order; the first matching handler processes
    // the request.
    class dap
    {
    public:
        dap();

        // Add a handler to the chain.
        void add_handler(std::unique_ptr<request_handler> handler);

        // Set the callback for sending async events to the client.
        void set_event_sender(std::function<void(const std::string &)> sender);

        // Run DAP server on the given streams.
        void run(std::istream &in, std::ostream &out);

    private:
        std::string handle_message(const std::string &json);
        std::string read_message(std::istream &in);
        void send_message(std::ostream &out, const std::string &json);

    private:
        std::vector<std::unique_ptr<request_handler>> handlers_;
        std::mutex mutex_;
        std::function<void(const std::string &)> send_event_;
    };

} // namespace dap
