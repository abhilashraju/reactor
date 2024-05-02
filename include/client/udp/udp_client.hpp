#pragma once
#include "common/common_defs.hpp"
#include "logger/logger.hpp"
namespace reactor
{
using udp = boost::asio::ip::udp;
using error_code = boost::system::error_code;
template <unsigned int BuffSize = 1024>
struct UdpClient
{
    udp::resolver resolver;
    udp::socket socket;
    std::optional<net::yield_context> yield;
    udp::endpoint endpoints;
    using on_receive_cb =
        std::function<void(error_code, std::string_view data)>;
    std::string rec_buffer;
    on_receive_cb on_receive;
    bool checkFailed(error_code ec, std::string_view message = "")
    {
        if (ec)
        {
            REACTOR_LOG_ERROR("{} : {}", message, ec.message());
            return true;
        }
        return false;
    }
    UdpClient(net::io_context& ioc, on_receive_cb&& h) :
        resolver(ioc), socket(ioc, udp::v4()), on_receive(std::move(h))
    {
        rec_buffer.resize(BuffSize);
    }
    void setYieldContext(net::yield_context y)
    {
        yield = y;
    }
    bool resolve(std::string_view host, std::string_view port)
    {
        error_code ec{};
        endpoints = *resolver.async_resolve(host, port, yield.value()[ec]);
        return !checkFailed(ec, "resolve");
    }

    bool send(net::const_buffer message, const udp::endpoint& endpoints)
    {
        error_code ec{};
        socket.async_send_to(message, endpoints, yield.value()[ec]);
        return !checkFailed(ec, "send");
    }
    bool send(net::const_buffer message)
    {
        return send(message, endpoints);
    }
    auto read()
    {
        error_code ec{};
        udp::endpoint sender_endpoint;
        auto bytes = socket.async_receive_from(
            net::buffer(rec_buffer), sender_endpoint, yield.value()[ec]);
        return checkFailed(ec, "read") ? 0 : bytes;
    }

    static void send_to(net::io_context& ioc, net::yield_context yield,
                        std::string_view host, std::string_view port,
                        net::const_buffer message, on_receive_cb&& h,
                        bool read = true)
    {
        UdpClient client(ioc, std::move(h));
        client.setYieldContext(yield);
        if (!client.resolve(host, port))
        {
            return;
        }
        if (!client.send(message))
        {
            return;
        }
        if (read)
        {
            auto bytes = client.read();
            if (bytes > 0)
            {
                client.on_receive(
                    {}, std::string_view(client.rec_buffer.data(), bytes));
            }
        }
    }
    static void send_to(net::io_context& ioc, net::yield_context yield,
                        std::string_view host, std::string_view port,
                        net::const_buffer message)
    {
        auto cb = [](error_code ec, net::const_buffer data) {
            if (ec)
            {
                REACTOR_LOG_ERROR("Error in udp : {}", ec.message());
            }
        };
        send_to(ioc, yield, host, port, message, cb, false);
    }
    static void send_to(net::io_context& ioc, net::yield_context yield,
                        const udp::endpoint& ep, net::const_buffer message)
    {
        auto cb = [](error_code ec, std::string_view data) {
            if (ec)
            {
                REACTOR_LOG_ERROR("Error in udp : {}", ec.message());
            }
        };
        UdpClient client(ioc, std::move(cb));
        client.setYieldContext(yield);
        client.send(message, ep);
    }
};
} // namespace reactor
