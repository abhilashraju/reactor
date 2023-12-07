#include "http/http_subscriber.hpp"

#include <gtest/gtest.h>
using namespace reactor;
TEST(HttpSubscriberTest, SendEvent)
{
    net::io_context ioContext;
    auto executor = net::make_strand(ioContext);

    // Create an instance of HttpSubscriber
    std::string destUrl = "https://localhost:8443/events";
    reactor::HttpSubscriber subscriber(executor, destUrl);

    // Define the expected data
    std::string data = "Test data";

    // Call the sendEvent function
    subscriber.sendEvent(data);

    ioContext.run();
}
