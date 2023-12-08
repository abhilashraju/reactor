#include "http/http_client_pool.hpp"

#include <boost/circular_buffer.hpp>
#include <boost/url/src.hpp>
#include <boost/url/url.hpp>
#include <boost/url/url_view.hpp>

#include <map>
#include <string>
namespace reactor
{
class HttpSubscriber
{
    using Session = AsyncSslSession<http::string_body>;
    using Request = Session::Request;
    struct RetryPolicy
    {
        int maxRetries{3};
        unsigned retryCount{0};
        unsigned retryDelay{5};
    };
    struct RetryRequest : std::enable_shared_from_this<RetryRequest>
    {
        Request req;
        RetryPolicy policy;
        boost::asio::steady_timer timer;
        std::function<void()> retryFunction;
        RetryRequest(Request&& r, const RetryPolicy& p,
                     const net::any_io_executor& ex) :
            req(std::move(r)),
            policy(p), timer(ex)
        {}
        ~RetryRequest()
        {
            std::cout << "RetryRequest destroyed" << std::endl;
        }
        void setRequest(Request&& r)
        {
            req = std::move(r);
        }
        void waitAndRetry()
        {
            if (policy.retryCount < policy.maxRetries)
            {
                policy.retryCount++;
                timer.expires_after(std::chrono::seconds(policy.retryDelay));
                timer.async_wait([self = shared_from_this()](
                                     const boost::system::error_code& ec) {
                    if (!ec)
                    {
                        self->retryFunction();
                    }
                });
            }
        }
    };

  public:
    HttpSubscriber(const net::any_io_executor& ioc, std::string destUrl) :
        ioContext(ioc), destUrl(destUrl), httpClientPool(ioContext, 5)
    {
        ctx.set_verify_mode(ssl::verify_none);
    }
    void processResponse(std::shared_ptr<Session>& session,
                         const HttpExpected<Session::Response>& response)
    {
        // Process the response
        const auto& res = response.response();
        std::cout << "Response status: " << res.result_int() << std::endl;
        std::cout << "Response body: " << res.body() << std::endl;
        if (!res.keep_alive())
        {
            httpClientPool.release(session);
        }
        sendNext();
    }
    void sendNext()
    {
        if (eventBuffer.empty())
        {
            return;
        }

        auto next = std::move(eventBuffer.front());
        eventBuffer.pop_front();
        sendEvent(next);
    }
    void handleRetryResponse(std::weak_ptr<Session> session,
                             std::shared_ptr<RetryRequest> retryRequest,
                             const HttpExpected<Session::Response>& response)
    {
        auto ptr = session.lock();
        if (response.isError())
        {
            std::cerr << "Error: " << response.error().message() << std::endl;
            retryRequest->setRequest(ptr->takeRequest());
            httpClientPool.release(ptr);
            retryRequest->waitAndRetry();

            return;
        }
        processResponse(ptr, response);
    }
    void handleResponse(std::weak_ptr<Session> session,
                        const HttpExpected<Session::Response>& response)
    {
        auto ptr = session.lock();
        if (response.isError())
        {
            std::cerr << "Error: " << response.error().message() << std::endl;
            httpClientPool.release(ptr);
            retryIfNeeded(ptr->takeRequest());
            return;
        }

        processResponse(ptr, response);
    }
    void sendEvent(const std::string& data)
    {
        // Acquire a session from the HttpClientPool
        auto session = httpClientPool.acquire(
            [&](std::shared_ptr<Session>& session) {
            boost::urls::url_view urlvw(destUrl);
            std::string h = urlvw.host();
            std::string p = urlvw.port();
            std::string path = urlvw.path();
            session->setOptions(Host{h}, Port{p}, Target{path}, Version{11},
                                Verb{http::verb::post}, KeepAlive{true},
                                ContentType{"application/json"});

            session->setResponseHandler(
                std::bind_front(&HttpSubscriber::handleResponse, this,
                                std::weak_ptr<Session>(session)));
        },
            ctx);
        // Send the data using the acquired session
        if (session)
        {
            session->setOption(data);
            session->run();
            return;
        }
        eventBuffer.push_back(data);
    }

  private:
    const net::any_io_executor& ioContext;
    std::string destUrl;
    HttpClientPool<Session> httpClientPool;
    ssl::context ctx{ssl::context::tlsv12_client};
    RetryPolicy retryPolicy;
    boost::circular_buffer<std::string> eventBuffer{10};

    void retryIfNeeded(Request&& req)
    {
        auto retryRequest = std::make_shared<RetryRequest>(
            std::move(req), retryPolicy, ioContext);
        retryRequest->retryFunction =
            [retrySelf = std::weak_ptr<RetryRequest>(retryRequest), this]() {
            if (auto retryRequest = retrySelf.lock())
            {
                auto session = httpClientPool.acquire(
                    [&retryRequest, this](std::shared_ptr<Session>& session) {
                    session->setOption(
                        Host{retryRequest->req.base()[http::field::host]});
                    session->setOption(Port{retryRequest->req.base()["port"]});
                    session->setResponseHandler(std::bind_front(
                        &HttpSubscriber::handleRetryResponse, this,
                        std::weak_ptr(session), retryRequest));
                },
                    ctx);
                session->run(std::move(retryRequest->req));
            }
        };
        retryRequest->waitAndRetry();
    }
};
} // namespace reactor
