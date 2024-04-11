
#include "streams.hpp"
namespace reactor
{

template <typename StreamMaker, typename Handler>
struct AsyncServer
{
    net::io_context ioc_;
    Handler& handler;
    tcp::acceptor acceptor_;
    StreamMaker streamMaker;

    AsyncServer(Handler& h, std::string_view port, StreamMaker&& streamMaker) :
        handler(h),
        acceptor_(ioc_, tcp::endpoint(tcp::v4(), std::atoi(port.data()))),
        streamMaker(std::move(streamMaker))
    {
        handler.setIoContext(std::ref(ioc_));
    }
    void start()
    {
        acceptor_.listen(net::socket_base::max_listen_connections);
        waitForAsyncConnection();
        ioc_.run();
    }
    void waitForAsyncConnection()
    {
        auto asyncWork = [this](auto streamReader, net::yield_context yield) {
            handler.handleRead(std::move(streamReader), yield);
        };
        streamMaker.acceptAsyncConnection(ioc_, acceptor_,
                                          std::move(asyncWork));
    }
    net::io_context& getIoContext()
    {
        return ioc_;
    }
};

template <typename Handler, typename StreamMaker = SslStreamMaker>
struct AsyncSslServer : AsyncServer<StreamMaker, Handler>
{
    using Base = AsyncServer<StreamMaker, Handler>;
    AsyncSslServer(Handler& handler, const std::string_view port,
                   std::string_view cirtDir) :
        Base(handler, port, StreamMaker(cirtDir))
    {}
};
} // namespace reactor
