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
        res.body() = beast::buffers_to_string(req.body().data());
        res.prepare_payload();
        return res;
    }
};
TestServer server;

TEST(webclient, simple_mono)
{
    net::io_context ioc;
    auto ex = net::make_strand(ioc);
    auto mono = WebClient<AsyncTcpStream>::builder()
                    .withSession(ex)
                    .withEndpoint("https://127.0.0.1:8081/testget")
                    .create()
                    .get()
                    .toMono();
    mono->subscribe([](auto v) {
        if (!v.isError())
        {
            std::cout << v.response().body();
            EXPECT_EQ(v.response().body(), "hello");
        }
    });
    ioc.run();
}
TEST(webclient, simple_flux)
{
    net::io_context ioc;
    auto ex = net::make_strand(ioc);
    std::vector<std::string> actual;
    ssl::context ctx{ssl::context::tlsv12_client};
    ctx.set_verify_mode(ssl::verify_none);
    auto flux = WebClient<SslStream>::builder()
                    .withSession(ex, ctx)
                    .withEndpoint("https://127.0.0.1:8081/testget")
                    .create()
                    .get()
                    .toFlux();
    flux->subscribe([&actual, i = 0](auto v, auto reqNext) mutable {
        if (!v.isError())
        {
            actual.push_back(v.response().body());
            reqNext(i++ < 2);
            return;
        }
        reqNext(false);
    });
    ioc.run();
    std::vector<std::string> expected{"test", "hello"};
    EXPECT_EQ(std::equal(begin(actual), end(actual), begin(expected)), true);
}
TEST(webclient, simple_flux_with_retry)
{
    net::io_context ioc;
    auto ex = net::make_strand(ioc);
    int trycount = 0;
    ssl::context ctx{ssl::context::tlsv12_client};
    ctx.set_verify_mode(ssl::verify_none);
    auto flux = WebClient<AsyncTcpStream>::builder()
                    .withSession(ex)
                    .withEndpoint("http://127.0.0.1:8081/testget")
                    .create()
                    .get()
                    .withRetry(3)
                    .toFlux();
    flux->asJson([&trycount, i = 0](auto v) mutable {
        trycount++;
        if (v.isError())
            throw std::runtime_error("error");
    });
    ioc.run();

    EXPECT_EQ(trycount, 3);
}
TEST(webclient, simple_mono_post)
{
    net::io_context ioc;
    auto ex = net::make_strand(ioc);
    http::string_body::value_type body = "test value";
    nlohmann::json j = {{"mytext", "test value"}};
    ssl::context ctx{ssl::context::tlsv12_client};
    ctx.set_verify_mode(ssl::verify_none);
    auto mono = WebClient<AsyncTcpStream, http::string_body>::builder()
                    .withSession(ex)
                    .withEndpoint("https://127.0.0.1:8081/testpost")
                    .create()
                    .post()
                    .withBody(std::move(j))
                    .toMono();

    mono->asJson([](auto v) {
        nlohmann::json newj = {{"mytext", "test value"}};
        EXPECT_EQ(v.response().data().dump(), newj.dump());
    });
    ioc.run();
}

TEST(webclient, simple_flux_post)
{
    net::io_context ioc;
    auto ex = net::make_strand(ioc);
    http::string_body::value_type body = "test value";

    auto flux = WebClient<AsyncTcpStream, http::string_body>::builder()
                    .withSession(ex)
                    .withEndpoint("https://127.0.0.1:8081/testpost")
                    .create()
                    .post()
                    .withContentType(ContentType{"plain/text"})
                    .withBody(std::move(body))
                    .toFlux();
    std::vector<std::string> actual;
    flux->subscribe([&actual, i = 0](auto v, auto reqNext) mutable {
        if (!v.isError())
        {
            actual.push_back(v.response().body());
            reqNext(i++ < 2);
            return;
        }
        reqNext(false);
    });
    ioc.run();
    std::vector<std::string> expected{"test value", "test value"};
    EXPECT_EQ(std::equal(begin(actual), end(actual), begin(expected)), true);
}
