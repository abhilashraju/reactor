#pragma once
#include "common/beast_defs.hpp"

#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>

#include <iostream>
namespace reactor
{

struct TcpClient
{
    tcp::resolver resolver;
    ssl::stream<tcp::socket> socket;
    TcpClient(net::io_context& ioc, ssl::context& ctx) :
        resolver(ioc), socket(ioc, ctx)
    {}
    bool connect(std::string_view host, std::string_view port,
                 net::yield_context yield)
    {
        beast::error_code ec{};
        auto endpoints = resolver.async_resolve(host, port, yield[ec]);
        if (checkFailed(ec))
        {
            return false;
        }
        net::async_connect(socket.next_layer(), endpoints, yield[ec]);
        if (checkFailed(ec))
        {
            return false;
        }
        socket.async_handshake(ssl::stream_base::client, yield[ec]);
        if (checkFailed(ec))
        {
            return false;
        }
        return true;
    }
    bool send(std::string_view message, net::yield_context yield)
    {
        beast::error_code ec{};
        net::async_write(socket, net::buffer(message), yield[ec]);
        if (checkFailed(ec))
        {
            return false;
        }
        return true;
    }
    bool read(auto& buffer, net::yield_context yield)
    {
        beast::error_code ec{};
        net::async_read(socket, buffer, yield[ec]);
        if (checkFailed(ec))
        {
            return false;
        }
        return true;
    }
    bool checkFailed(beast::error_code& ec)
    {
        if (ec)
        {
            std::cout << ec.message() << "\n";
            return true;
        }
        return false;
    }
};
} // namespace reactor
