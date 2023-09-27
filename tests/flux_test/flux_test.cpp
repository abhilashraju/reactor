#include "http/http_client.hpp"
#include "http/web_client.hpp"
#include "http_server.hpp"

#include <exec/single_thread_context.hpp>
#include <utilities.hpp>

#include <filesystem>
#include <fstream>
#include <map>

#include "gtest/gtest.h"
using namespace reactor;

class TestServer
{
    exec::single_thread_context ctx;
    chai::HttpServer server{"8081"};
    std::thread* runner{nullptr};

  public:
    TestServer()
    {
        server.router().add_get_handler(
            "/testget", std::bind_front(&TestServer::testHandler, this));
        server.router().add_post_handler(
            "/testpost", std::bind_front(&TestServer::testPostHandler, this));

        runner = new std::thread([&]() { server.start(ctx); });
    }
    ~TestServer() {}

    chai::VariantResponse testHandler(const chai::DynamicbodyRequest& req,
                                      const chai::http_function& httpfunc)
    {
        chai::http::response<chai::http::string_body> res{http::status::ok,
                                                          req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.body() = "hello";
        res.prepare_payload();
        return res;
    }
    chai::VariantResponse testPostHandler(const chai::DynamicbodyRequest& req,
                                          const chai::http_function& httpfunc)
    {
        chai::http::response<chai::http::string_body> res{http::status::ok,
                                                          req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.body() = "hello";
        res.prepare_payload();
        return res;
    }
};
TestServer server;
TEST(flux, just_int)
{
    bool finished{false};
    std::vector<std::string> captured;
    auto ins = std::back_inserter(captured);
    auto m2 = Flux<std::string>::range(std::vector<std::string>{"hi", "hello"});
    m2.onFinish([&finished]() { finished = true; }).subscribe([&ins](auto v) {
        *ins = v;
    });
    std::vector totest = {"hi", "hello"};
    EXPECT_EQ(std::equal(begin(captured), end(captured), begin(totest)), true);
    EXPECT_EQ(finished, true);
}
TEST(flux, flux_connection)
{
    net::io_context ioc;
    auto ex = net::make_strand(ioc);

    auto m2 = HttpFlux<http::string_body>::connect(
        AsyncTcpSession<http::empty_body>::create(ex, AsyncTcpStream(ex)),
        "https://127.0.0.1:8081/testget");

    m2.subscribe([](auto v) { EXPECT_EQ(v.response().body(), "hello"); });

    ioc.run();
}
TEST(flux, flux_connection_sink)
{
    net::io_context ioc;
    auto ex = net::make_strand(ioc);

    auto m2 = HttpFlux<http::string_body>::connect(
        AsyncTcpSession<http::empty_body>::create(ex, AsyncTcpStream(ex)),
        "https://127.0.0.1:8081/testget");

    auto sink = createHttpSink(
        AsyncTcpSession<http::string_body>::create(ex, AsyncTcpStream(ex)));
    sink.setUrl("https://127.0.0.1:8081/testpost")
        .onData([](auto& res, bool& needNext) {
            EXPECT_EQ(res.response().body(), "hello");
        });

    m2.subscribe(std::move(sink));
    ioc.run();
}

TEST(flux, flux_connection_broadcast_sink)
{
    net::io_context ioc;
    auto ex = net::make_strand(ioc);

    ssl::context ctx{ssl::context::tlsv12_client};
    ctx.set_verify_mode(ssl::verify_none);
    auto m2 = HttpFlux<http::string_body>::connect(
        AsyncSslSession<http::empty_body>::create(ex, AsyncSslStream(ex, ctx)),
        "https://127.0.0.1:8443/testget");

    auto sink1 = createHttpSink(AsyncSslSession<http::string_body>::create(
        ex, AsyncSslStream(ex, ctx)));
    sink1.setUrl("https://127.0.0.1:8443/testpost")
        .onData([](auto& res, bool& needNext) {
            EXPECT_EQ(res.response().body(), "hello");
        });

    auto sink2 = createHttpSink(AsyncSslSession<http::string_body>::create(
        ex, AsyncSslStream(ex, ctx)));
    sink2.setUrl("https://127.0.0.1:8443/testpost")
        .onData([i = 0](auto& res, bool& needNext) mutable {
            if (!res.isError())
            {
                EXPECT_EQ(res.response().body(), "hello");
                if (i++ < 5)
                    needNext = true;
                return;
            }
            std::cout << res.error().what() << "\n" << res.response();
        });

    m2.subscribe(
        createStringBodyBroadCaster(std::move(sink1), std::move(sink2)));

    ioc.run();
}
