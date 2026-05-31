// main.cpp
// DAP TCP server main loop.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.

#include <iostream>
#include <sockpp/tcp_acceptor.h>
#include <sockpp/tcp_socket.h>

#include <dap/dap.h>
#include <dbg.h>
#include <socket_stream.h>

constexpr int PORT = 4711;

int main()
{
    sockpp::initialize();
    sockpp::tcp_acceptor acc(PORT);
    if (!acc) {
        std::cerr << "Error creating acceptor: " << acc.last_error_str() << "\n";
        return 1;
    }

    std::cerr << "DAP server listening on port " << PORT << "\n";

    while (true) {
        sockpp::inet_address peer;
        sockpp::tcp_socket sock = acc.accept(&peer);
        if (!sock) {
            std::cerr << "Error accepting: " << acc.last_error_str() << "\n";
            continue;
        }
        std::cerr << "\n--- Client connected: " << peer.to_string() << " ---\n";

        socket_stream_buffer buf(sock);
        std::istream in(&buf);
        std::ostream out(&buf);

        dap::dap dispatcher;
        dbg debug_instance;

        // Wire event delivery: dbg (and background emulation thread)
        // → dispatcher.send_event_direct() which is output-mutex-protected.
        auto event_sender = [&dispatcher](const std::string &ev) {
            dispatcher.send_event_direct(ev);
        };
        debug_instance.set_event_sender(event_sender);

        debug_instance.register_handlers(dispatcher);
        dispatcher.run(in, out);

        // Ensure any running emulation is stopped cleanly before the next
        // connection (dbg destructor also does this, but being explicit).
        debug_instance.stop_emulation();

        std::cerr << "--- Client disconnected ---\n";
    }

    return 0;
}
