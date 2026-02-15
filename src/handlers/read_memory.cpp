// read_memory.cpp — DAP "readMemory" request handler.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <dap/dap.h>
#include <dap/handler.h>
#include <dbg.h>

namespace handlers {

namespace {

std::string base64_encode(const uint8_t *data, size_t len)
{
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3)
    {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < len) n |= static_cast<uint32_t>(data[i + 2]);
        out.push_back(table[(n >> 18) & 0x3F]);
        out.push_back(table[(n >> 12) & 0x3F]);
        out.push_back((i + 1 < len) ? table[(n >> 6) & 0x3F] : '=');
        out.push_back((i + 2 < len) ? table[n & 0x3F] : '=');
    }
    return out;
}

} // namespace

class read_memory_handler : public dap::request_handler {
public:
    read_memory_handler(dbg &ctx) : ctx_(ctx) {}
    std::string command() const override { return "readMemory"; }

    std::string handle(const dap::request &req) override
    {
        auto r = dap::read_memory_request::from(req);

        int start = r.memory_reference + r.offset;
        auto &mem = ctx_.memory();
        int count = std::max(0,
            std::min(r.count, static_cast<int>(mem.size()) - start));

        std::string data = base64_encode(
            mem.data() + start, static_cast<size_t>(count));

        dap::response resp(r.seq, r.command);
        resp.success(true).result({
            {"address", ctx_.format_hex(static_cast<uint16_t>(start), 4)},
            {"data", data},
            {"unreadableBytes", 0}});
        return resp.str();
    }

private:
    dbg &ctx_;
};

std::unique_ptr<dap::request_handler> make_read_memory(dbg &ctx)
{
    return std::make_unique<read_memory_handler>(ctx);
}

} // namespace handlers
