#include "http/http_subscriber.hpp"

#include <gtest/gtest.h>
using namespace reactor;
TEST(HttpSubscriberTest, SendEvent)
{
    net::io_context ioContext;

    auto executor = ioContext.get_executor();

    // Create an instance of HttpSubscriber
    std::string destUrl = "https://localhost:8443/events";
    ssl::context ctx{ssl::context::tlsv12_client};
    ctx.set_verify_mode(ssl::verify_none);
    reactor::HttpSubscriber subscriber(executor, destUrl);
    subscriber
        .withPolicy(
            HttpSubscriber::RetryPolicy{.maxRetries = 3, .retryDelay = 15})
        .withSslContext(std::move(ctx))
        .withPoolSize(1)
        .withSuccessHandler([](const auto& req, const auto& res) {
        std::cout << "Success: " << req.body() << std::endl;
    });

    // Define the expected data
    std::string data = R"(
        {
  "Events": [
    {
      "Context": "",
      "EventId": "TestID",
      "EventTimestamp": "2023-11-23T06:21:54+00:00",
      "EventType": "Event",
      "Message": "Generated test event",
      "MessageArgs": [ ],
      "MessageId": "OpenBMC.0.2.TestEventLog",
      "Severity": "OK"
    }
  ]
}
  
)";

    // Call the sendEvent function
    int i = 0;
    while (i < 100)
    {
        auto newdata = data;
        newdata.replace(newdata.find("TestID"), 6,
                        "TestID" + std::to_string(i++));
        subscriber.sendEvent(newdata);
    }

    ioContext.run();
}
