#pragma once
#include "http_client_pool.hpp"
#include "retry_request.hpp"

#include <boost/circular_buffer.hpp>
#include <boost/url/url.hpp>
#include <boost/url/url_view.hpp>

#include <map>
#include <string>
namespace reactor
{
class HttpSubscriber
{
  public:
    using Session = AsyncSslSession<http::string_body>;
    using Request = Session::Request;
    using Response = Session::Response;
    using RetryRequest = RetryRequest<Request>;

  private:
  public:
    HttpSubscriber(net::any_io_executor ioc, std::string destUrl) :
        ioContext(ioc), destUrl(destUrl), httpClientPool(ioContext, 5)
    {
        ctx.set_verify_mode(ssl::verify_none);
    }
    ~HttpSubscriber()
    {
        CLIENT_LOG_INFO("HttpSubscriber destroyed");
    }
    HttpSubscriber(HttpSubscriber&&) = default;
    HttpSubscriber& withPolicy(const RetryPolicy& policy)
    {
        retryPolicy = policy;
        return *this;
    }
    HttpSubscriber& withSslContext(ssl::context&& sslctx)
    {
        ctx = std::move(sslctx);
        return *this;
    }
    HttpSubscriber& withSuccessHandler(
        std::function<void(const Request&, const Response&)>&& handler)
    {
        successHandler = std::move(handler);
        return *this;
    }
    HttpSubscriber& withPoolSize(std::size_t poolSize)
    {
        httpClientPool.withPoolSize(poolSize);
        return *this;
    }
    HttpSubscriber& withHeaders(Headers aheaders)
    {
        headers = std::move(aheaders);
        return *this;
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
            CLIENT_LOG_INFO("host: {}, port: {}, path: {}", h, p, path);
            session->setOptions(Host{h}, Port{p}, Target{path}, Version{11},
                                Verb{http::verb::post}, KeepAlive{true},
                                ContentType{"application/json"}, headers);

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
    void processResponse(std::shared_ptr<Session>& session, const Request& req,
                         const HttpExpected<Session::Response>& response)
    {
        // Process the response
        const auto& res = response.response();
        CLIENT_LOG_INFO("Response status: {}", res.result_int());
        CLIENT_LOG_INFO("Response body: {}", res.body());
        if (successHandler)
        {
            successHandler(req, res);
        }
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
                             const Request& req,
                             const HttpExpected<Session::Response>& response)
    {
        auto ptr = session.lock();
        if (response.isError())
        {
            CLIENT_LOG_ERROR("Error: {}", response.error().message());
            retryRequest->setRequest(ptr->takeRequest());
            httpClientPool.release(ptr);
            retryRequest->waitAndRetry();

            return;
        }
        processResponse(ptr, req, response);
    }
    void handleResponse(std::weak_ptr<Session> session, const Request& req,
                        const HttpExpected<Session::Response>& response)
    {
        auto ptr = session.lock();
        if (response.isError())
        {
            CLIENT_LOG_ERROR("Error: {}", response.error().message());
            httpClientPool.release(ptr);
            retryIfNeeded(ptr->takeRequest());
            return;
        }

        processResponse(ptr, req, response);
    }

  private:
    net::any_io_executor ioContext;
    std::string destUrl;
    HttpClientPool<Session> httpClientPool;
    ssl::context ctx{ssl::context::tlsv12_client};
    Headers headers;
    RetryPolicy retryPolicy;
    boost::circular_buffer<std::string> eventBuffer{100};
    std::function<void(const Request&, const Response&)> successHandler;

    void retryIfNeeded(Request&& req)
    {
        if (!retryPolicy.retryNeeded())
        {
            return;
        }
        auto retryRequest = std::make_shared<RetryRequest>(
            std::move(req), retryPolicy, ioContext);
        retryRequest->retryFunction =
            [retrySelf = std::weak_ptr<RetryRequest>(retryRequest), this]() {
            if (auto retryRequest = retrySelf.lock())
            {
                auto session = httpClientPool.acquire(
                    [&retryRequest, this](std::shared_ptr<Session>& session) {
                    session->setOption(retryRequest->req.base());
                    session->setOption(
                        Host{retryRequest->req.base()[http::field::host]});
                    session->setOption(Port{retryRequest->req.base()["port"]});

                    session->setResponseHandler(std::bind_front(
                        &HttpSubscriber::handleRetryResponse, this,
                        std::weak_ptr(session), retryRequest));
                },
                    ctx);
                if (session)
                { // got free session from pool
                    session->run(std::move(retryRequest->req));
                    return;
                }
                // failed to get a session . So retry again by reducing the
                // retry count
                retryRequest->policy.decrementRetryCount();
                retryRequest->waitAndRetry();
            }
        };
        retryRequest->waitAndRetry();
    }
};
} // namespace reactor
