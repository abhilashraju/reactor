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
            "/testpost", std::bind_front(&TestServer::testHandler, this));

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

    using SourceSession = HttpSession<ASyncTcpStream>;
    std::shared_ptr<SourceSession> session =
        SourceSession::create(ex, ASyncTcpStream(ex));
    auto m2 = HttpFlux<http::string_body>::connect(
        session, "https://127.0.0.1:8081/testget");

    m2.subscribe([](auto v) { EXPECT_EQ(v.body(), "hello"); });

    ioc.run();
}
TEST(flux, flux_connection_sink)
{
    net::io_context ioc;
    auto ex = net::make_strand(ioc);

    using SourceSession = HttpSession<ASyncTcpStream>;
    using SinkSession = HttpSession<ASyncTcpStream, http::string_body>;

    auto m2 = HttpFlux<http::string_body>::connect(
        SourceSession::create(ex, ASyncTcpStream(ex)),
        "https://127.0.0.1:8081/testget");

    HttpSink<SinkSession> sink(SinkSession::create(ex, ASyncTcpStream(ex)));
    sink.setUrl("https://127.0.0.1:8081/testpost")
        .onData(
            [](auto& res, bool& needNext) { EXPECT_EQ(res.body(), "hello"); });

    m2.subscribe(std::move(sink));
    ioc.run();
}
