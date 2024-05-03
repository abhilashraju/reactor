#pragma once
#include "common/common_defs.hpp"
#include "logger/logger.hpp"

#include <boost/system/error_code.hpp>
namespace reactor
{
using udp = boost::asio::ip::udp;
using error_code = boost::system::error_code;
template <typename Handler, size_t BuffSize = 1024>
class UdpServer
{
  public:
    UdpServer(std::string_view p, Handler& h) :
        port(p), socket_(ioc_, udp::endpoint(udp::v4(), atoi(p.data()))),
        handler(h)
    {
        buffer_.resize(BuffSize);
    }

    void start()
    {
        acceptAsyncConnection();
        ioc_.run();
    }
    void acceptAsyncConnection()
    {
        net::spawn(ioc_, std::bind_front(&UdpServer::do_receive, this));
    }
    net::io_context& getIoContext()
    {
        return ioc_;
    }
    udp::endpoint getLocalEndpoint()
    {
        return socket_.local_endpoint();
    }

  private:
    void do_receive(net::yield_context yield)
    {
        udp::endpoint sender_endpoint;
        error_code ec{};
        auto bytes_received = socket_.async_receive_from(
            net::buffer(buffer_), sender_endpoint, yield[ec]);
        if (ec)
        {
            REACTOR_LOG_ERROR("Error receiving data: {}", ec.message());
            bytes_received = 0;
        }
        acceptAsyncConnection();
        auto sendcb = [this, sender_endpoint](auto&& res,
                                              net::yield_context y) {
            socket_.async_send_to(net::const_buffer(res.data(), res.size()),
                                  sender_endpoint, y);
        };
        handler.handleRead(ec, std::string_view{buffer_.data(), bytes_received},
                           sender_endpoint, yield,
                           [this, sendcb = std::move(sendcb)](auto&& res) {
            net::spawn(ioc_, std::bind_front(sendcb, std::move(res)));
        });
    }

    std::string port;
    net::io_context ioc_;
    udp::socket socket_;

    std::string buffer_;
    Handler& handler;
};
} // namespace reactor
