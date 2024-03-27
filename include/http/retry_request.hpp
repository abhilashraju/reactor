#pragma once
#include <boost/asio/steady_timer.hpp>

#include <functional>
#include <memory>
namespace reactor
{
struct RetryPolicy
{
    int maxRetries{3};
    unsigned retryCount{0};
    unsigned retryDelay{15};
    bool retryNeeded() const
    {
        return maxRetries < 0 || retryCount < maxRetries;
    }
    void incrementRetryCount()
    {
        retryCount++;
    }
    void decrementRetryCount()
    {
        retryCount--;
    }
    auto getRetryDelay() const
    {
        return std::chrono::seconds(retryDelay);
    }
};
template <typename Request>
struct RetryRequest : std::enable_shared_from_this<RetryRequest<Request>>
{
    using Base = std::enable_shared_from_this<RetryRequest<Request>>;
    Request req;
    RetryPolicy policy;
    boost::asio::steady_timer timer;
    std::function<void()> retryFunction;
    RetryRequest(Request&& r, const RetryPolicy& p, net::any_io_executor ex) :
        req(std::move(r)), policy(p), timer(ex)
    {}
    ~RetryRequest()
    {
        CLIENT_LOG_INFO("RetryRequest destroyed");
    }
    void setRequest(Request&& r)
    {
        req = std::move(r);
    }
    void waitAndRetry()
    {
        if (policy.retryNeeded())
        {
            policy.incrementRetryCount();
            timer.expires_after(policy.getRetryDelay());
            timer.async_wait([self = Base::shared_from_this()](
                                 const boost::system::error_code& ec) {
                if (!ec)
                {
                    self->retryFunction();
                }
            });
        }
    }
};
} // namespace reactor
