// socket_stream.h
// iostream streambuf adapter for sockpp TCP sockets.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#pragma once

#include <streambuf>
#include <sockpp/tcp_socket.h>

// Adapts a sockpp::tcp_socket into a std::streambuf so it can be used
// directly with std::istream / std::ostream.
class socket_stream_buffer : public std::streambuf {
public:
    explicit socket_stream_buffer(sockpp::tcp_socket& socket);

protected:
    int underflow() override;
    int overflow(int c) override;
    int sync() override;

private:
    sockpp::tcp_socket& socket_;
    static constexpr size_t kBufSize = 4096;
    char in_buf_[kBufSize];
    char out_buf_[kBufSize];
};
