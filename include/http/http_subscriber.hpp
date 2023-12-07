#include "http/http_client_pool.hpp"

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

  public:
    HttpSubscriber(const net::any_io_executor& ioContext, std::string destUrl) :
        destUrl(destUrl), httpClientPool(ioContext, 5)
    {
        ctx.set_verify_mode(ssl::verify_none);
    }
    void handleResponse(const HttpExpected<Session::Response>& response,
                        std::shared_ptr<Session> session)
    {
        // Handle the response
        if (response.isError())
        {
            // Error handling
            std::cerr << "Error: " << response.error().message() << std::endl;
            httpClientPool.release(session);
        }
        else
        {
            // Process the response
            const auto& res = response.response();
            std::cout << "Response status: " << res.result_int() << std::endl;
            std::cout << "Response body: " << res.body() << std::endl;
        }
    }
    void sendEvent(const std::string& data)
    {
        // Acquire a session from the HttpClientPool
        auto session = httpClientPool.acquire(
            [&](std::shared_ptr<Session> session) {
            boost::urls::url_view urlvw(destUrl);
            std::string h = urlvw.host();
            std::string p = urlvw.port();
            std::string path = urlvw.path();
            session->setOptions(Host{h}, Port{p}, Target{path}, Version{11},
                                Verb{http::verb::post}, KeepAlive{true},
                                ContentType{"application/json"});

            session->setResponseHandler(
                std::bind(&HttpSubscriber::handleResponse, this,
                          std::placeholders::_1, session));
        },
            ctx);

        // Send the data using the acquired session
        session->setOption(data);
        session->run();
    }

  private:
    std::string destUrl;
    HttpClientPool<Session> httpClientPool;
    ssl::context ctx{ssl::context::tlsv12_client};
};
} // namespace reactor
