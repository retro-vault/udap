// response.cpp
// Response builder for Debug Adapter Protocol (DAP).
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include "dap.h"

namespace dap
{
    response::response(int request_seq, const std::string &command)
    {
        _json["type"]        = "response";
        _json["request_seq"] = request_seq;
        _json["command"]     = command;
        // seq is injected by dap::handle_message() so it can use the
        // dispatcher's monotonic counter.
    }

    response &response::success(bool ok)
    {
        _json["success"] = ok;
        return *this;
    }

    response &response::message(const std::string &msg)
    {
        _json["message"] = msg;
        return *this;
    }

    response &response::result(const json &result_data)
    {
        _json["body"] = result_data;
        return *this;
    }

    std::string response::str() const
    {
        // Ensure success is always present.
        if (!_json.contains("success"))
            const_cast<json &>(_json)["success"] = true;
        return _json.dump();
    }

} // namespace dap
