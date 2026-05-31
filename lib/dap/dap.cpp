// dap.cpp
// DAP dispatcher implementation with chain of responsibility.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <dap/dap.h>

namespace dap
{
    // ---------------------------------------------------------------------------
    // request::parse — the one remaining free function not inlined in dap.h
    // ---------------------------------------------------------------------------

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

    void dap::add_handler(std::unique_ptr<request_handler> handler)
    {
        std::lock_guard lock(dispatch_mutex_);
        handlers_.push_back(std::move(handler));
    }

    void dap::register_handler(const std::string &command,
                               std::function<std::string(const std::string &)> fn)
    {
        add_handler(std::make_unique<raw_lambda_handler>(command, std::move(fn)));
    }

    void dap::send_event_direct(const std::string &event_json)
    {
        if (!out_) return;
        std::lock_guard lock(output_mutex_);
        *out_ << "Content-Length: " << event_json.size() << "\r\n\r\n"
              << event_json << std::flush;
    }

    std::string dap::handle_message(const std::string &json_text)
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

    std::string dap::read_message(std::istream &in)
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

    void dap::send_message(std::ostream &out, const std::string &json)
    {
        std::lock_guard lock(output_mutex_);
        out << "Content-Length: " << json.size() << "\r\n\r\n"
            << json << std::flush;
    }

    void dap::run(std::istream &in, std::ostream &out)
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
