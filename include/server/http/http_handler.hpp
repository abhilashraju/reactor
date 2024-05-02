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
            auto responseHander = [streamReader](auto&& response) mutable {
                net::spawn(streamReader.stream().get_executor(),
                           [streamReader, response = std::move(response)](
                               net::yield_context yield) {
                    sendResponse(streamReader, response, yield);
                });
            };
            (*forwarder)(std::move(request), std::move(responseHander));
        }
    }
    static inline void sendResponse(auto streamReader, auto& response,
                                    net::yield_context yield)
    {
        beast::error_code ec{};
        auto resp = std::get_if<StringbodyResponse>(&response);
        assert(resp);
        http::async_write(streamReader.stream(), *resp, yield[ec]);

        streamReader.stream().async_shutdown(yield[ec]);
        if (ec)
        {
            std::cout << ec.message() << "\n";
        }
        streamReader.stream().lowest_layer().close();
    }
};
} // namespace reactor
