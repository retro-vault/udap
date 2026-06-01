// dap.cpp
// DAP dispatcher implementation with chain of responsibility.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <iostream>
#include "dap.h"

namespace dap
{
    // ---------------------------------------------------------------------------
    // request::parse — the one remaining free function not inlined in dap.h
    // ---------------------------------------------------------------------------

    // -------------------------------------------------------------------------
    // Typed request constructors — JSON parsing lives here, not in the header.
    // -------------------------------------------------------------------------

    initialize_request::initialize_request(const request &b) : request(b) {
        adapter_id  = b.arguments.value("adapterID",   "");
        client_id   = b.arguments.value("clientID",    "");
        client_name = b.arguments.value("clientName",  "");
        locale      = b.arguments.value("locale",      "");
    }

    launch_request::launch_request(const request &b) : request(b) {
        no_debug = b.arguments.value("noDebug",  false);
        program  = b.arguments.value("program",  "");
        cdb_file = b.arguments.value("cdbFile",  "");
        map_file = b.arguments.value("mapFile",  "");
    }

    set_breakpoints_request::set_breakpoints_request(const request &b) : request(b) {
        const auto &src = b.arguments.value("source", json::object());
        if (src.contains("path"))            source_path      = src["path"].get<std::string>();
        else if (src.contains("name"))       source_path      = src["name"].get<std::string>();
        if (src.contains("sourceReference")) source_reference = src["sourceReference"].get<int>();
        for (const auto &bp : b.arguments.value("breakpoints", json::array()))
            lines.push_back(bp.value("line", 1));
    }

    stack_trace_request::stack_trace_request(const request &b) : request(b) {
        thread_id   = b.arguments.value("threadId",   0);
        start_frame = b.arguments.value("startFrame", 0);
        levels      = b.arguments.value("levels",     0);
    }

    scopes_request::scopes_request(const request &b) : request(b) {
        frame_id = b.arguments.value("frameId", 0);
    }

    variables_request::variables_request(const request &b) : request(b) {
        variables_reference = b.arguments.value("variablesReference", 0);
    }

    continue_request::continue_request(const request &b) : request(b) {
        thread_id = b.arguments.value("threadId", 0);
    }

    source_request::source_request(const request &b) : request(b) {
        if (b.arguments.contains("sourceReference"))
            source_reference = b.arguments["sourceReference"].get<int>();
        else if (b.arguments.contains("source") &&
                 b.arguments["source"].contains("sourceReference"))
            source_reference = b.arguments["source"]["sourceReference"].get<int>();
    }

    read_memory_request::read_memory_request(const request &b) : request(b) {
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

    disassemble_request::disassemble_request(const request &b) : request(b) {
        if (b.arguments.contains("memoryReference"))
            memory_reference = static_cast<int>(
                std::stoul(b.arguments["memoryReference"].get<std::string>(), nullptr, 0));
        offset             = b.arguments.value("offset",            0);
        instruction_offset = b.arguments.value("instructionOffset", 0);
        instruction_count  = b.arguments.value("instructionCount",  0);
    }

    breakpoint_locations_request::breakpoint_locations_request(const request &b) : request(b) {
        source   = b.arguments.value("source",  json::object());
        line     = b.arguments.value("line",    0);
        end_line = b.arguments.value("endLine", 0);
        if (end_line == 0) end_line = line;
    }

    set_function_breakpoints_request::set_function_breakpoints_request(const request &b) : request(b) {
        breakpoints = b.arguments.value("breakpoints", std::vector<json>{});
    }

    set_instruction_breakpoints_request::set_instruction_breakpoints_request(const request &b) : request(b) {
        breakpoints = b.arguments.value("breakpoints", std::vector<json>{});
    }

    next_request::next_request(const request &b) : request(b) {
        thread_id = b.arguments.value("threadId", 0);
    }

    step_in_request::step_in_request(const request &b) : request(b) {
        thread_id   = b.arguments.value("threadId",    0);
        granularity = b.arguments.value("granularity", "");
    }

    step_out_request::step_out_request(const request &b) : request(b) {
        thread_id   = b.arguments.value("threadId",    0);
        granularity = b.arguments.value("granularity", "");
    }

    step_back_request::step_back_request(const request &b) : request(b) {
        thread_id   = b.arguments.value("threadId",    0);
        granularity = b.arguments.value("granularity", "");
    }

    evaluate_request::evaluate_request(const request &b) : request(b) {
        expression = b.arguments.value("expression", "");
        context    = b.arguments.value("context",    "");
        frame_id   = b.arguments.value("frameId",    0);
    }

    // -------------------------------------------------------------------------

    request request::parse(const std::string &json_text)
    {
        json j = json::parse(json_text); // throws nlohmann::json::parse_error on bad input
        request base;
        base.seq       = j.value("seq",       0);
        base.type      = j.value("type",      "");
        base.command   = j.value("command",   "");
        base.arguments = j.value("arguments", json::object());
        return base;
    }
    // --- Lambda adapter (for register_handler convenience method) ----------

    class lambda_handler : public request_handler {
    public:
        lambda_handler(std::string cmd,
                       std::function<std::string(const std::string &)> fn)
            : cmd_(std::move(cmd)), fn_(std::move(fn)) {}

        std::string command() const override { return cmd_; }
        std::string handle(const request &req) override {
            return fn_(req.arguments.dump());  // pass raw arguments JSON
        }
    private:
        std::string cmd_;
        std::function<std::string(const std::string &)> fn_;
    };

    // lambda_handler::handle receives the full raw JSON for the message, not
    // just arguments — the test passes the full message string to the lambda.
    // Override to pass the full original JSON instead.
    class raw_lambda_handler : public request_handler {
    public:
        raw_lambda_handler(std::string cmd,
                           std::function<std::string(const std::string &)> fn)
            : cmd_(std::move(cmd)), fn_(std::move(fn)) {}

        std::string command() const override { return cmd_; }
        std::string handle(const request &req) override {
            // Reconstruct the full message JSON for the lambda.
            json j;
            j["seq"] = req.seq;
            j["type"] = req.type;
            j["command"] = req.command;
            j["arguments"] = req.arguments;
            return fn_(j.dump());
        }
    private:
        std::string cmd_;
        std::function<std::string(const std::string &)> fn_;
    };

    // -----------------------------------------------------------------------

    void session::add_handler(std::unique_ptr<request_handler> handler)
    {
        std::lock_guard lock(dispatch_mutex_);
        handlers_.push_back(std::move(handler));
    }

    void session::register_handler(const std::string &command,
                               std::function<std::string(const std::string &)> fn)
    {
        add_handler(std::make_unique<raw_lambda_handler>(command, std::move(fn)));
    }

    void session::send_event_direct(const std::string &event_json)
    {
        if (!out_) return;
        std::lock_guard lock(output_mutex_);
        *out_ << "Content-Length: " << event_json.size() << "\r\n\r\n"
              << event_json << std::flush;
    }

    std::string session::handle_message(const std::string &json_text)
    {
        request req;
        try {
            req = request::parse(json_text);
        } catch (...) {
            response resp(0, "<unknown>");
            resp.success(false).message("Malformed request");
            return resp.str();
        }

        std::string result;
        {
            std::lock_guard lock(dispatch_mutex_);
            for (auto &handler : handlers_) {
                if (handler->command() == req.command) {
                    result = handler->handle(req);
                    break;
                }
            }
        }

        if (result.empty()) {
            response resp(req.seq, req.command);
            resp.success(false).message("Unknown command: " + req.command);
            result = resp.str();
        }

        // Inject monotonic response seq required by the DAP spec.
        try {
            auto j = json::parse(result);
            if (j.value("type", "") == "response")
                j["seq"] = response_seq_++;
            result = j.dump();
        } catch (...) {}

        return result;
    }

    std::string session::read_message(std::istream &in)
    {
        int content_length = -1;
        std::string line;

        // Read headers line-by-line until the blank separator line.
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            if (line.empty())
                break; // blank line = end of headers

            if (line.rfind("Content-Length:", 0) == 0) {
                try {
                    content_length = std::stoi(line.substr(15));
                } catch (...) {
                    std::cerr << "[dap] malformed Content-Length header: "
                              << line << "\n";
                }
            }
        }

        if (!in)
            return {};

        if (content_length < 0) {
            std::cerr << "[dap] no Content-Length header; disconnecting\n";
            return {};
        }

        std::string payload(static_cast<size_t>(content_length), '\0');
        in.read(payload.data(), content_length);
        if (in.gcount() != content_length)
            return {};

        return payload;
    }

    void session::send_message(std::ostream &out, const std::string &json)
    {
        std::lock_guard lock(output_mutex_);
        out << "Content-Length: " << json.size() << "\r\n\r\n"
            << json << std::flush;
    }

    void session::run(std::istream &in, std::ostream &out)
    {
        out_ = &out;

        while (true) {
            std::string req_json = read_message(in);
            if (req_json.empty()) break;

            std::cerr << "[RECV] " << req_json << "\n";

            std::string resp_json = handle_message(req_json);
            if (!resp_json.empty()) {
                std::cerr << "[SEND] " << resp_json << "\n";
                send_message(out, resp_json);
            }
        }

        out_ = nullptr;
    }

} // namespace dap
