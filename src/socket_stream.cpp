// socket_stream.cpp
// iostream streambuf adapter for sockpp TCP sockets.
//
// Copyright 2025 Tomaz Stih. All rights reserved.
// MIT License.
#include <socket_stream.h>

socket_stream_buffer::socket_stream_buffer(sockpp::tcp_socket& socket)
    : socket_(socket)
{
    setg(in_buf_, in_buf_, in_buf_);
    setp(out_buf_, out_buf_ + kBufSize);
}

int socket_stream_buffer::underflow() {
    if (gptr() < egptr())
        return traits_type::to_int_type(*gptr());

    auto n = socket_.read(in_buf_, kBufSize);
    if (n <= 0)
        return traits_type::eof();

    setg(in_buf_, in_buf_, in_buf_ + n);
    return traits_type::to_int_type(*gptr());
}

int socket_stream_buffer::overflow(int c) {
    if (c != traits_type::eof()) {
        *pptr() = static_cast<char>(c);
        pbump(1);
    }
    return sync() == 0 ? c : traits_type::eof();
}

int socket_stream_buffer::sync() {
    auto size = pptr() - pbase();
    if (size > 0) {
        if (socket_.write_n(pbase(), size) != size)
            return -1;
        setp(out_buf_, out_buf_ + kBufSize);
    }
    return 0;
}
