// main.cpp
// DAP TCP server entry point.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <iostream>
#include <sockpp/tcp_acceptor.h>
#include <sockpp/tcp_socket.h>

#include <socket_stream.h>
#include <z80_target.h>

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

        z80_target target;
        target.run(in, out);

        std::cerr << "--- Client disconnected ---\n";
    }
}
