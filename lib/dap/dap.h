// dap.h
// Core DAP types: requests, response, dispatcher.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <mutex>
#include <optional>

#include <nlohmann/json.hpp>

namespace dap
{
    using json = nlohmann::json;

    // Base request — all typed structs inherit from this.
    struct request {
        int seq = 0;
        std::string type;
        std::string command;
        json arguments;
        static request parse(const std::string &json_text);
        virtual ~request() = default;
    };

    // Abstract handler (chain of responsibility, used internally).
    class request_handler {
    public:
        virtual ~request_handler() = default;
        virtual std::string command() const = 0;
        virtual std::string handle(const request &req) = 0;
    };

    // -------------------------------------------------------------------------
    // Typed request structs — fields only, constructors defined in dap.cpp.
    // -------------------------------------------------------------------------

    struct initialize_request : request {
        std::string adapter_id, client_id, client_name, locale;
        explicit initialize_request(const request &b);
        static initialize_request from(const request &r) { return initialize_request(r); }
    };

    struct launch_request : request {
        bool no_debug = false;
        std::string program, cdb_file, map_file;
        explicit launch_request(const request &b);
        static launch_request from(const request &r) { return launch_request(r); }
    };

    struct attach_request : request {
        explicit attach_request(const request &b) : request(b) {}
        static attach_request from(const request &r) { return attach_request(r); }
    };

    struct set_breakpoints_request : request {
        std::string source_path;
        int source_reference = 0;
        std::vector<int> lines;
        explicit set_breakpoints_request(const request &b);
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
        explicit stack_trace_request(const request &b);
        static stack_trace_request from(const request &r) { return stack_trace_request(r); }
    };

    struct scopes_request : request {
        int frame_id = 0;
        explicit scopes_request(const request &b);
        static scopes_request from(const request &r) { return scopes_request(r); }
    };

    struct variables_request : request {
        int variables_reference = 0;
        explicit variables_request(const request &b);
        static variables_request from(const request &r) { return variables_request(r); }
    };

    struct continue_request : request {
        int thread_id = 0;
        explicit continue_request(const request &b);
        static continue_request from(const request &r) { return continue_request(r); }
    };

    struct source_request : request {
        int source_reference = 0;
        explicit source_request(const request &b);
        static source_request from(const request &r) { return source_request(r); }
    };

    struct read_memory_request : request {
        uint16_t memory_reference = 0;
        int offset = 0, count = 0;
        explicit read_memory_request(const request &b);
        static read_memory_request from(const request &r) { return read_memory_request(r); }
    };

    struct disassemble_request : request {
        int memory_reference = 0, offset = 0, instruction_offset = 0, instruction_count = 0;
        explicit disassemble_request(const request &b);
        static disassemble_request from(const request &r) { return disassemble_request(r); }
    };

    struct breakpoint_locations_request : request {
        json source;
        int line = 0, end_line = 0;
        explicit breakpoint_locations_request(const request &b);
        static breakpoint_locations_request from(const request &r) { return breakpoint_locations_request(r); }
    };

    struct set_function_breakpoints_request : request {
        std::vector<json> breakpoints;
        explicit set_function_breakpoints_request(const request &b);
        static set_function_breakpoints_request from(const request &r) { return set_function_breakpoints_request(r); }
    };

    struct set_instruction_breakpoints_request : request {
        std::vector<json> breakpoints;
        explicit set_instruction_breakpoints_request(const request &b);
        static set_instruction_breakpoints_request from(const request &r) { return set_instruction_breakpoints_request(r); }
    };

    struct next_request : request {
        int thread_id = 0;
        explicit next_request(const request &b);
        static next_request from(const request &r) { return next_request(r); }
    };

    struct step_in_request : request {
        int thread_id = 0;
        std::string granularity;
        explicit step_in_request(const request &b);
        static step_in_request from(const request &r) { return step_in_request(r); }
    };

    struct step_out_request : request {
        int thread_id = 0;
        std::string granularity;
        explicit step_out_request(const request &b);
        static step_out_request from(const request &r) { return step_out_request(r); }
    };

    struct step_back_request : request {
        int thread_id = 0;
        std::string granularity;
        explicit step_back_request(const request &b);
        static step_back_request from(const request &r) { return step_back_request(r); }
    };

    struct evaluate_request : request {
        std::string expression, context;
        int frame_id = 0;
        explicit evaluate_request(const request &b);
        static evaluate_request from(const request &r) { return evaluate_request(r); }
    };

    // -------------------------------------------------------------------------
    // Response builder (fluent).
    // -------------------------------------------------------------------------

    class response {
    public:
        response(int request_seq, const std::string &command);
        response &success(bool ok);
        response &message(const std::string &msg);
        response &result(const json &result_data);
        std::string str() const;
    private:
        json _json;
    };

    // -------------------------------------------------------------------------
    // DAP dispatcher (chain of responsibility).
    // -------------------------------------------------------------------------

    class session {
    public:
        void add_handler(std::unique_ptr<request_handler> handler);
        void register_handler(const std::string &command,
                              std::function<std::string(const std::string &)> fn);
        template <typename ReqT>
        void register_typed_handler(const std::string &command,
                                    std::function<std::string(const ReqT &)> fn);
        void send_event_direct(const std::string &event_json);
        void run(std::istream &in, std::ostream &out);
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

    // -------------------------------------------------------------------------
    // Template implementation (must stay in header).
    // -------------------------------------------------------------------------

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
    void session::register_typed_handler(const std::string &command,
                                         std::function<std::string(const ReqT &)> fn) {
        add_handler(std::make_unique<typed_handler_adapter<ReqT>>(command, std::move(fn)));
    }

} // namespace dap
