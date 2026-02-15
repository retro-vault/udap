// dap.cpp
// DAP dispatcher implementation with chain of responsibility.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <dap/dap.h>
#include <dap/handler.h>

namespace dap
{
    dap::dap() = default;

    void dap::add_handler(std::unique_ptr<request_handler> handler)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        handlers_.push_back(std::move(handler));
    }

    std::string dap::handle_message(const std::string &json_text)
    {
        request req;
        try
        {
            req = request::parse(json_text);
        }
        catch (...)
        {
            response resp(0, "<unknown>");
            resp.success(false).message("Malformed request");
            return resp.str();
        }

        std::lock_guard<std::mutex> lock(mutex_);

        // Walk the handler chain (chain of responsibility).
        for (auto &handler : handlers_)
        {
            if (handler->command() == req.command)
                return handler->handle(req);
        }

        response resp(req.seq, req.command);
        resp.success(false).message("Unknown command: " + req.command);
        return resp.str();
    }

    void dap::set_event_sender(std::function<void(const std::string &)> sender)
    {
        send_event_ = std::move(sender);
    }


    std::string dap::read_message(std::istream &in)
    {
        std::string header;
        char c;
        while (in && header.find("\r\n\r\n") == std::string::npos)
        {
            c = in.get();
            if (in.eof()) return "";
            header += c;
        }

        auto pos = header.find("Content-Length:");
        if (pos == std::string::npos) return "";

        size_t len_start = pos + 15;
        size_t len_end = header.find("\r\n", len_start);
        if (len_end == std::string::npos) return "";

        int content_length = std::stoi(header.substr(len_start, len_end - len_start));

        std::string payload(content_length, '\0');
        in.read(&payload[0], content_length);

        return payload;
    }

    void dap::send_message(std::ostream &out, const std::string &json)
    {
        out << "Content-Length: " << json.size() << "\r\n\r\n";
        out << json;
        out.flush();
    }

    void dap::run(std::istream &in, std::ostream &out)
    {
        while (true)
        {
            std::string req_json = read_message(in);
            if (req_json.empty()) break;

            std::cout << "\n[RECEIVED] " << req_json << "\n";

            std::string resp_json = handle_message(req_json);
            if (!resp_json.empty()) {
                std::cout << "[RESPONSE] " << resp_json << "\n";
                send_message(out, resp_json);
            }
        }
    }

} // namespace dap
