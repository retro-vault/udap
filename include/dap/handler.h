// handler.h
// Convenience base class and factory template for DAP request handlers.
//
// All handler implementation files should include this header.  It provides:
//   - dbg_handler : the boilerplate-free base for handlers that hold a dbg&
//   - make_handler<T>(dbg&) : one-line factory wrapper
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#pragma once

#include <dap/dap.h>
#include <dbg.h>

// Base class that stores ctx_ and provides the constructor.
// Handlers inherit from this with `using dbg_handler::dbg_handler;` to
// get the constructor for free.
class dbg_handler : public dap::request_handler {
public:
    explicit dbg_handler(dbg &ctx) : ctx_(ctx) {}
protected:
    dbg &ctx_;
};

// One-line factory: return make_handler<foo_handler>(ctx);
template <typename T>
std::unique_ptr<dap::request_handler> make_handler(dbg &ctx) {
    return std::make_unique<T>(ctx);
}
