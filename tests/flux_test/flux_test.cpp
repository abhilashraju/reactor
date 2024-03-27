#include "http/http_client.hpp"
#include "http/web_client.hpp"
#include "http_server.hpp"

#include <exec/static_thread_pool.hpp>
#include <utilities.hpp>

#include <filesystem>
#include <fstream>
#include <map>

#include "gtest/gtest.h"
using namespace reactor;

class TestServer
{
    exec::static_thread_pool ctx;
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

TEST(flux, just_int_with_map)
{
    bool finished{false};
    std::vector<std::string> captured;
    auto ins = std::back_inserter(captured);
    auto m2 = Flux<std::string>::range(std::vector<std::string>{"hi", "hello"});

    m2.map([](const auto& v) { return v.length(); })
        .map([](const auto& v) {
        return v >= 5;
    }).subscribe([](auto v, auto next) {
        std::cout << "value " << v << "\n";
        next(true);
    });
    // std::vector totest = {"hi", "hello"};
    // EXPECT_EQ(std::equal(begin(captured), end(captured), begin(totest)),
    // true); EXPECT_EQ(finished, true);
}

TEST(flux, generator)
{
    bool finished{false};
    auto m2 = Flux<std::string>::generate(
        [myvec = std::vector<std::string>{"hi", "hello"},
         i = 0](bool& hasNext) mutable {
        auto ret = myvec.at(i++);
        hasNext = i < myvec.size();
        return ret;
    });
    std::vector<std::string> captured;
    auto ins = std::back_inserter(captured);
    m2.subscribe([&ins](auto v, auto next) {
        *ins = v;
        next(true);
    });
    std::vector expected = {"hi", "hello"};
    EXPECT_EQ(std::equal(begin(captured), end(captured), begin(expected)),
              true);
}
TEST(flux, generator_with_filter)
{
    bool finished{false};
    auto m2 = Flux<std::string>::generate(
        [myvec = std::vector<std::string>{"hi", "hello"},
         i = 0](bool& hasNext) mutable {
        auto ret = myvec.at(i++);
        hasNext = i < myvec.size();
        return ret;
    });
    std::vector<std::string> captured;
    auto ins = std::back_inserter(captured);
    m2.filter([](const auto& v) {
        return v == "hi";
    }).subscribe([&ins](auto v, auto next) {
        *ins = v;
        next(true);
    });
    std::vector expected = {"hi"};
    EXPECT_EQ(std::equal(begin(captured), end(captured), begin(expected)),
              true);
}
TEST(flux, generator_with_filter_and_map)
{
    bool finished{false};
    auto m2 = Flux<std::string>::generate(
        [myvec = std::vector<std::string>{"hi", "hello"},
         i = 0](bool& hasNext) mutable {
        auto ret = myvec.at(i++);
        hasNext = i < myvec.size();
        return ret;
    });
    std::vector<int> captured;
    auto ins = std::back_inserter(captured);
    m2.filter([](const auto& v) { return v == "hi"; })
        .map([](auto&& v) {
        return v.length();
    }).subscribe([&ins](auto v, auto next) {
        *ins = v;
        next(true);
    });
    std::vector expected = {2};
    EXPECT_EQ(std::equal(begin(captured), end(captured), begin(expected)),
              true);
}
TEST(flux, flux_connection)
{
    net::io_context ioc;
    auto ex = net::make_strand(ioc);
    using Session = AsyncTcpSession<http::empty_body>;
    auto m2 = HttpFlux<Session>::connect(Session::create(ex),
                                         "http://127.0.0.1:8081/testget");

    m2.subscribe([](auto v, auto&& reqNext) {
        EXPECT_EQ(v.response().body(), "hello");
    });

    ioc.run();
}
TEST(flux, flux_connection_sink)
{
    net::io_context ioc;
    auto ex = net::make_strand(ioc);
    using Session = AsyncTcpSession<http::empty_body>;
    auto m2 = HttpFlux<Session>::connect(Session::create(ex),
                                         "http://127.0.0.1:8081/testget");

    auto sink = createHttpSink<decltype(m2)::SourceType>(
        AsyncTcpSession<http::string_body>::create(ex));
    sink.setUrl("http://127.0.0.1:8081/testpost")
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
    using Session = AsyncTcpSession<http::empty_body>;
    auto m2 = HttpFlux<Session>::connect(Session::create(ex),
                                         "http://127.0.0.1:8081/testget");

    auto sink1 = createHttpSink<decltype(m2)::SourceType>(
        AsyncTcpSession<http::string_body>::create(ex));
    sink1.setUrl("http://127.0.0.1:8081/testpost")
        .onData([](auto& res, bool& needNext) {
        EXPECT_EQ(res.response().body(), "hello");
    });

    auto sink2 = createHttpSink<decltype(m2)::SourceType>(
        AsyncTcpSession<http::string_body>::create(ex));
    sink2.setUrl("http://127.0.0.1:8081/testpost")
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

TEST(flux, generator_http_sink)
{
    net::io_context ioc;
    auto ex = net::make_strand(ioc);

    ssl::context ctx{ssl::context::tlsv12_client};
    ctx.set_verify_mode(ssl::verify_none);

    auto m2 = Flux<std::string>::generate([i = 1](bool& hasNext) mutable {
        std::string ret("hello ");
        ret += std::to_string(i++);
        return ret;
    });
    auto sink2 = createHttpSink<std::string>(
        AsyncTcpSession<http::string_body>::create(ex));
    int i = 1;
    sink2.setUrl("http://127.0.0.1:8443/testpost")
        .onData([&i](const auto& res, bool& needNext) mutable {
        if (!res.isError())
        {
            std::string expected("hello ");
            expected += std::to_string(i++);
            EXPECT_EQ(res.response().body(), expected);
            std::cout << res.response().body() << "\n";
            if (i < 5)
                needNext = true;
            return;
        }
        std::cout << res.error().what() << "\n" << res.response();
    });
    m2.subscribe(std::move(sink2));

    while (i < 5)
    {
        ioc.run();
    }
}
