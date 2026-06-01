// dap.h
// Core types and server logic for the Debug Adapter Protocol (DAP).
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
#include <optional>

#include <nlohmann/json.hpp>

namespace dap
{
    using json = nlohmann::json;

    // Generic request wrapper. Base for all typed request structs.
    struct request
    {
        int seq = 0;
        std::string type;
        std::string command;
        json arguments;

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

    // ---------------------------------------------------------------------------
    // Typed request structs — each has an explicit converting constructor that
    // extracts its fields from the base request's arguments JSON.
    // The static from() wrappers keep call sites unchanged.
    // ---------------------------------------------------------------------------

    struct initialize_request : request {
        std::string adapter_id, client_id, client_name, locale;
        explicit initialize_request(const request &b) : request(b) {
            adapter_id  = b.arguments.value("adapterID",   "");
            client_id   = b.arguments.value("clientID",    "");
            client_name = b.arguments.value("clientName",  "");
            locale      = b.arguments.value("locale",      "");
        }
        static initialize_request from(const request &r) { return initialize_request(r); }
    };

    struct launch_request : request {
        bool no_debug = false;
        std::string program, cdb_file, map_file;
        explicit launch_request(const request &b) : request(b) {
            no_debug = b.arguments.value("noDebug",  false);
            program  = b.arguments.value("program",  "");
            cdb_file = b.arguments.value("cdbFile",  "");
            map_file = b.arguments.value("mapFile",  "");
        }
        static launch_request from(const request &r) { return launch_request(r); }
    };

    struct attach_request : request {
        explicit attach_request(const request &b) : request(b) {}
        static attach_request from(const request &r) { return attach_request(r); }
    };

    struct set_breakpoints_request : request {
        std::string source_path;
        int         source_reference = 0; // non-zero for virtual/disassembly sources
        std::vector<int> lines;
        explicit set_breakpoints_request(const request &b) : request(b) {
            const auto &src = b.arguments.value("source", json::object());
            if (src.contains("path"))            source_path      = src["path"].get<std::string>();
            else if (src.contains("name"))       source_path      = src["name"].get<std::string>();
            if (src.contains("sourceReference")) source_reference = src["sourceReference"].get<int>();
            for (const auto &bp : b.arguments.value("breakpoints", json::array()))
                lines.push_back(bp.value("line", 1));
        }
        static set_breakpoints_request from(const request &r) { return set_breakpoints_request(r); }
    };

    struct configuration_done_request : request {
        explicit configuration_done_request(const request &b) : request(b) {}
        static configuration_done_request from(const request &r) { return configuration_done_request(r); }
    };

    struct threads_request : request {
        explicit threads_request(const request &b) : request(b) {}
        static threads_request from(const request &r) { return threads_request(r); }
    };

    struct stack_trace_request : request {
        int thread_id = 0, start_frame = 0, levels = 0;
        explicit stack_trace_request(const request &b) : request(b) {
            thread_id   = b.arguments.value("threadId",   0);
            start_frame = b.arguments.value("startFrame", 0);
            levels      = b.arguments.value("levels",     0);
        }
        static stack_trace_request from(const request &r) { return stack_trace_request(r); }
    };

    struct scopes_request : request {
        int frame_id = 0;
        explicit scopes_request(const request &b) : request(b) {
            frame_id = b.arguments.value("frameId", 0);
        }
        static scopes_request from(const request &r) { return scopes_request(r); }
    };

    struct variables_request : request {
        int variables_reference = 0;
        explicit variables_request(const request &b) : request(b) {
            variables_reference = b.arguments.value("variablesReference", 0);
        }
        static variables_request from(const request &r) { return variables_request(r); }
    };

    struct continue_request : request {
        int thread_id = 0;
        explicit continue_request(const request &b) : request(b) {
            thread_id = b.arguments.value("threadId", 0);
        }
        static continue_request from(const request &r) { return continue_request(r); }
    };

    struct source_request : request {
        int source_reference = 0;
        explicit source_request(const request &b) : request(b) {
            if (b.arguments.contains("sourceReference"))
                source_reference = b.arguments["sourceReference"].get<int>();
            else if (b.arguments.contains("source") &&
                     b.arguments["source"].contains("sourceReference"))
                source_reference = b.arguments["source"]["sourceReference"].get<int>();
        }
        static source_request from(const request &r) { return source_request(r); }
    };

    struct read_memory_request : request {
        uint16_t memory_reference = 0;
        int offset = 0, count = 0;
        explicit read_memory_request(const request &b) : request(b) {
            if (b.arguments.contains("memoryReference")) {
                const auto &ref = b.arguments["memoryReference"];
                uint32_t raw = ref.is_string()
                    ? static_cast<uint32_t>(std::stoul(ref.get<std::string>(), nullptr, 0))
                    : ref.get<uint32_t>();
                memory_reference = static_cast<uint16_t>(raw & 0xFFFF);
            }
            offset = b.arguments.value("offset", 0);
            count  = b.arguments.value("count",  0);
        }
        static read_memory_request from(const request &r) { return read_memory_request(r); }
    };

    struct disassemble_request : request {
        int memory_reference = 0, offset = 0, instruction_offset = 0, instruction_count = 0;
        explicit disassemble_request(const request &b) : request(b) {
            if (b.arguments.contains("memoryReference"))
                memory_reference = static_cast<int>(
                    std::stoul(b.arguments["memoryReference"].get<std::string>(), nullptr, 0));
            offset            = b.arguments.value("offset",           0);
            instruction_offset= b.arguments.value("instructionOffset",0);
            instruction_count = b.arguments.value("instructionCount", 0);
        }
        static disassemble_request from(const request &r) { return disassemble_request(r); }
    };

    struct breakpoint_locations_request : request {
        json source;
        int line = 0, end_line = 0;
        explicit breakpoint_locations_request(const request &b) : request(b) {
            source   = b.arguments.value("source", json::object());
            line     = b.arguments.value("line",    0);
            end_line = b.arguments.value("endLine", 0);
            if (end_line == 0) end_line = line;
        }
        static breakpoint_locations_request from(const request &r) { return breakpoint_locations_request(r); }
    };

    struct set_function_breakpoints_request : request {
        std::vector<json> breakpoints;
        explicit set_function_breakpoints_request(const request &b) : request(b) {
            breakpoints = b.arguments.value("breakpoints", std::vector<json>{});
        }
        static set_function_breakpoints_request from(const request &r) { return set_function_breakpoints_request(r); }
    };

    struct set_instruction_breakpoints_request : request {
        std::vector<json> breakpoints;
        explicit set_instruction_breakpoints_request(const request &b) : request(b) {
            breakpoints = b.arguments.value("breakpoints", std::vector<json>{});
        }
        static set_instruction_breakpoints_request from(const request &r) { return set_instruction_breakpoints_request(r); }
    };

    struct next_request : request {
        int thread_id = 0;
        explicit next_request(const request &b) : request(b) {
            thread_id = b.arguments.value("threadId", 0);
        }
        static next_request from(const request &r) { return next_request(r); }
    };

    struct step_in_request : request {
        int thread_id = 0;
        std::string granularity;
        explicit step_in_request(const request &b) : request(b) {
            thread_id   = b.arguments.value("threadId",    0);
            granularity = b.arguments.value("granularity", "");
        }
        static step_in_request from(const request &r) { return step_in_request(r); }
    };

    struct step_out_request : request {
        int thread_id = 0;
        std::string granularity;
        explicit step_out_request(const request &b) : request(b) {
            thread_id   = b.arguments.value("threadId",    0);
            granularity = b.arguments.value("granularity", "");
        }
        static step_out_request from(const request &r) { return step_out_request(r); }
    };

    struct step_back_request : request {
        int thread_id = 0;
        std::string granularity;
        explicit step_back_request(const request &b) : request(b) {
            thread_id   = b.arguments.value("threadId",    0);
            granularity = b.arguments.value("granularity", "");
        }
        static step_back_request from(const request &r) { return step_back_request(r); }
    };

    struct evaluate_request : request {
        std::string expression, context;
        int frame_id = 0;
        explicit evaluate_request(const request &b) : request(b) {
            expression = b.arguments.value("expression", "");
            context    = b.arguments.value("context",    "");
            frame_id   = b.arguments.value("frameId",    0);
        }
        static evaluate_request from(const request &r) { return evaluate_request(r); }
    };

    // ---------------------------------------------------------------------------
    // Response builder (fluent interface).
    // ---------------------------------------------------------------------------

    class response
    {
    public:
        response(int request_seq, const std::string &command);

        response &success(bool ok);
        response &message(const std::string &msg);
        response &result(const json &result_data);
        std::string str() const;

    private:
        json _json;
    };

    // ---------------------------------------------------------------------------
    // DAP dispatcher (chain of responsibility).
    // ---------------------------------------------------------------------------

    class dap
    {
    public:
        // Add a typed handler object to the chain.
        void add_handler(std::unique_ptr<request_handler> handler);

        // Lambda-based convenience registration (for tests / simple cases).
        void register_handler(const std::string &command,
                              std::function<std::string(const std::string &)> fn);

        // Typed-request convenience registration.
        template <typename ReqT>
        void register_typed_handler(const std::string &command,
                                    std::function<std::string(const ReqT &)> fn);

        // Send an event directly from any thread (mutex-protected).
        void send_event_direct(const std::string &event_json);

        // Run DAP server on the given streams (blocks until disconnect).
        void run(std::istream &in, std::ostream &out);

        // Exposed for tests.
        std::string handle_message(const std::string &json_text);

    private:
        std::string read_message(std::istream &in);
        void send_message(std::ostream &out, const std::string &json);

        std::vector<std::unique_ptr<request_handler>> handlers_;
        std::mutex dispatch_mutex_;
        std::mutex output_mutex_;
        std::ostream *out_{nullptr};
        int response_seq_{1};
    };

    // ---------------------------------------------------------------------------
    // Template implementations
    // ---------------------------------------------------------------------------

    template <typename ReqT>
    class typed_handler_adapter : public request_handler {
    public:
        typed_handler_adapter(std::string cmd, std::function<std::string(const ReqT &)> fn)
            : cmd_(std::move(cmd)), fn_(std::move(fn)) {}
        std::string command() const override { return cmd_; }
        std::string handle(const request &req) override { return fn_(ReqT(req)); }
    private:
        std::string cmd_;
        std::function<std::string(const ReqT &)> fn_;
    };

    template <typename ReqT>
    void dap::register_typed_handler(const std::string &command,
                                     std::function<std::string(const ReqT &)> fn) {
        add_handler(std::make_unique<typed_handler_adapter<ReqT>>(command, std::move(fn)));
    }

} // namespace dap
