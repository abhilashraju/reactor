#include "http/http_client.hpp"

#include <gtest/gtest.h>
using namespace reactor;
TEST(HttpSessionTest, RunTest)
{
    // Create an io_context and executor
    net::io_context ioContext;
    net::any_io_executor executor(ioContext.get_executor());

    // Create an HttpSession object
    using Session = AsyncSslSession<http::string_body>;
    ssl::context ctx{ssl::context::tlsv12_client};
    ctx.set_verify_mode(ssl::verify_none);

    auto session = Session::create(executor, ctx);

    // Set options for the session

    session->setOption(Session::RequestBody::value_type("Request body"));
    session->setOption(Target("/events"));
    session->setOption(Port("8443"));
    session->setOption(Host("127.0.0.1"));
    session->setOption(Verb(http::verb::post));
    session->setOption(Version(11));
    session->setOption(KeepAlive(true));
    session->setOption(ContentType("application/json"));

    // Set the response handler
    session->setResponseHandler(
        [](const HttpExpected<Session::Response>& response) {
        // Handle the response
        if (response.isError())
        {
            // Error handling
            std::cerr << "Error: " << response.error().message() << std::endl;
        }
        else
        {
            // Process the response
            const auto& res = response.response();
            std::cout << "Response status: " << res.result_int() << std::endl;
            std::cout << "Response body: " << res.body() << std::endl;
        }
    });

    // Run the session
    session->run();

    // Run the io_context to execute asynchronous operations
    ioContext.run();
}
