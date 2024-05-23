#pragma once
#include "server/tcp/streams.hpp"

namespace reactor
{
template <typename Router>
class HttpHandler
{
    Router& router;

  public:
    HttpHandler(Router& router) : router(router) {}
    void setIoContext(std::reference_wrapper<net::io_context> ioc)
    {
        router.setIoContext(ioc);
    }

    void handleRead(auto&& streamReader, net::yield_context yield)
    {
        handleRead(std::move(streamReader), router, yield);
    }
    static void handleRead(auto&& streamReader, auto& router,
                           net::yield_context yield)
    {
        beast::flat_buffer buffer;
        StringbodyRequest request;
        beast::error_code ec{};
        http::async_read(streamReader.stream(), buffer, request, yield[ec]);
        if (ec)
        {
            REACTOR_LOG_ERROR("Error reading request: ", ec.message());
            return;
        }
        handleRequest(std::move(request), std::move(streamReader), router,
                      yield);
    }
    static inline void handleRequest(auto&& request, auto&& streamReader,
                                     auto& router, net::yield_context yield)
    {
        auto forwarder = router.getForwarder(request.target());
        if (forwarder)
        {
            bool keepAlive = request.keep_alive();
            auto responseHander = [streamReader, &router,
                                   keepAlive](auto&& response) mutable {
                net::spawn(streamReader.stream().get_executor(),
                           [streamReader, keepAlive, &router,
                            response = std::move(response)](
                               net::yield_context yield) mutable {
                    sendResponse(streamReader, response, keepAlive, router,
                                 yield);
                });
            };
            (*forwarder)(std::move(request), streamReader.endPoint(),
                         std::move(responseHander));
        }
    }
    static inline void sendResponse(auto streamReader, auto& response,
                                    bool keepAlive, auto& router,
                                    net::yield_context yield)
    {
        beast::error_code ec{};
        auto resp = std::get_if<StringbodyResponse>(&response);
        assert(resp);
        resp->keep_alive(keepAlive);
        http::async_write(streamReader.stream(), *resp, yield[ec]);
        if (ec || !keepAlive)
        {
            streamReader.stream().async_shutdown(yield[ec]);
            if (ec)
            {
                std::cout << ec.message() << "\n";
            }
            streamReader.stream().lowest_layer().close();
            return;
        }
        handleRead(std::move(streamReader), router, yield);
    }
};
} // namespace reactor
