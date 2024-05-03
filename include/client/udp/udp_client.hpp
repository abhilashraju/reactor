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
        std::function<void(error_code, udp::endpoint, std::string_view data)>;
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
    void setBroadCast(bool b)
    {
        socket.set_option(udp::socket::broadcast(b));
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
    bool resolve(std::string_view port)
    {
        try
        {
            endpoints = udp::endpoint(net::ip::address_v4::broadcast(),
                                      std::stoi(port.data()));
        }
        catch (std::exception& e)
        {
            REACTOR_LOG_ERROR("Error in resolving endpoint : {}", e.what());
            return false;
        }
        return true;
    }

    bool send(net::const_buffer message, const udp::endpoint& ep)
    {
        error_code ec{};
        socket.async_send_to(message, ep, yield.value()[ec]);
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
        if (checkFailed(ec, "read"))
        {
            return 0;
        }
        if (bytes > 0)
        {
            on_receive({}, sender_endpoint,
                       std::string_view(rec_buffer.data(), bytes));
        }
    }
    auto cancelAfter(std::chrono::seconds sec)
    {
        boost::asio::steady_timer timer(socket.get_executor());
        timer.expires_after(sec);
        timer.async_wait([&](const boost::system::error_code& error) {
            if (!error)
            {
                socket.cancel();
            }
        });
        return timer;
    }
    void readFor(std::chrono::seconds sec)
    {
        auto now = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - now < sec)
        {
            error_code ec{};
            auto timer = cancelAfter(sec);
            udp::endpoint sender_endpoint;
            auto bytes = socket.async_receive_from(
                net::buffer(rec_buffer), sender_endpoint, yield.value()[ec]);
            if (ec)
            {
                REACTOR_LOG_ERROR("Error in readFor : {}", ec.message());
            }
            timer.cancel();
            if (bytes > 0)
            {
                on_receive({}, sender_endpoint,
                           std::string_view(rec_buffer.data(), bytes));
            }
        }
    }
    static void broadcast(net::io_context& ioc, net::yield_context yield,
                          std::string_view port, net::const_buffer message)
    {
        broadcast(ioc, std::move(yield), port, message,
                  [](error_code ec, std::string_view data) {
            if (ec)
            {
                REACTOR_LOG_ERROR("Error in udp : {}", ec.message());
            }
        });
    }
    static void broadcast(net::io_context& ioc, net::yield_context yield,
                          std::string_view port, net::const_buffer message,
                          on_receive_cb&& h, bool read = false,
                          std::chrono::seconds sec = std::chrono::seconds(15))
    {
        UdpClient client(ioc, std::move(h));
        client.setBroadCast(true);
        client.setYieldContext(yield);
        if (!client.resolve(port))
        {
            return;
        }
        client.send(message);
        if (read)
        {
            client.readFor(sec);
        }
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
            client.read();
        }
    }
    static void send_to(net::io_context& ioc, net::yield_context yield,
                        std::string_view host, std::string_view port,
                        net::const_buffer message)
    {
        auto cb = [](error_code ec, const auto& ep, std::string_view data) {
            if (ec)
            {
                REACTOR_LOG_ERROR("Error in udp : {}", ec.message());
            }
        };
        send_to(ioc, yield, host, port, message, std::move(cb), false);
    }
};
} // namespace reactor
