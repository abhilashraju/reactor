#pragma once
#include "core/reactor.hpp"
#include "http/http_client.hpp"

#include <boost/url/src.hpp>
#include <boost/url/url.hpp>
#include <boost/url/url_view.hpp>

#include <numeric>
#include <ranges>
namespace reactor
{

template <typename Res, typename Session>
struct HttpSource : FluxBase<Res>::SourceHandler
{
    std::shared_ptr<Session> session;
    std::string url;
    http::verb verb;
    int count{1};
    bool forever{false};
    explicit HttpSource(std::shared_ptr<Session> aSession, int shots,
                        bool infinite = false) :
        session(std::move(aSession)),
        count(shots), forever(infinite)
    {}
    void setUrl(std::string u)
    {
        url = std::move(u);
    }
    void setVerb(http::verb v)
    {
        verb = v;
    }
    void decrement()
    {
        if (count > 0)
        {
            count--;
        }
    }
    void next(std::function<void(Res)> consumer) override
    {
        decrement();
        session->setResponseHandler([consumer = std::move(consumer)](
                                        const Res& res) { consumer(res); });
        boost::urls::url_view urlvw(url);
        std::string h = urlvw.host();
        std::string p = urlvw.port();
        std::string path = urlvw.path();
        session->setOptions(Host{h}, Port{p}, Target{path}, Version{11},
                            Verb{verb}, KeepAlive{forever});
        session->run();
    }
    bool hasNext() const override
    {
        return forever || count > 0;
    }
    void stop()
    {
        forever = false;
    }
};

template <typename Session>
HttpSource(std::shared_ptr<Session>) -> HttpSource<std::string, Session>;

template <typename Body, bool flux>
struct HttpFluxBase : FluxBase<HttpExpected<http::response<Body>>>
{
    using SourceType = HttpExpected<http::response<Body>>;
    using Base = FluxBase<HttpExpected<http::response<Body>>>;
    explicit HttpFluxBase(Base::SourceHandler* srcHandler) : Base(srcHandler) {}
    template <typename Stream>
    static HttpFluxBase connect(std::shared_ptr<HttpSession<Stream>> session,
                                const std::string& url)
    {
        auto src = new HttpSource<SourceType, HttpSession<Stream>>(session, 1,
                                                                   flux);
        src->setUrl(url);
        src->setVerb(http::verb::get);
        auto m = HttpFluxBase{src};
        return m;
    }
};
template <typename Body>
using HttpFlux = HttpFluxBase<Body, true>;
template <typename Body>
using HttpMono = HttpFluxBase<Body, false>;

template <typename Session = HttpSession<AsyncSslStream, http::string_body>>
struct HttpSink
{
    using Response =
        HttpExpected<http::response<typename Session::ResponseBody>>;
    using ResponseHandler = std::function<void(const Response&, bool&)>;
    std::shared_ptr<Session> session;
    std::string url;
    ResponseHandler onDataHandler;
    explicit HttpSink(std::shared_ptr<Session> aSession) :
        session(std::move(aSession))
    {}
    ~HttpSink()
    {
        // std::cout << "Destructor Called for HttpSink";
    }
    HttpSink& setUrl(std::string u)
    {
        url = std::move(u);
        return *this;
    }
    HttpSink& onData(ResponseHandler dataHandler)
    {
        onDataHandler = std::move(dataHandler);
        return *this;
    }

    void operator()(const Response& res, auto&& requestNext)
    {
        session->setResponseHandler(
            [this, requestNext = std::move(requestNext)](const Response& res) {
            bool neednext{false};
            if (onDataHandler)
            {
                onDataHandler(std::move(res), neednext);
            }
            requestNext(neednext);
        });
        boost::urls::url_view urlvw(url);
        std::string h = urlvw.host();
        std::string p = urlvw.port();
        std::string path = urlvw.path();
        http::string_body::value_type body(res.response().body());

        session->setOptions(Host{h}, Port{p}, Target{path}, Version{11},
                            Verb{http::verb::post}, KeepAlive{true}, body,
                            ContentType{"plain/text"});
        session->run();
    }
};
template <typename Session>
inline auto createHttpSink(std::shared_ptr<Session> aSession)
{
    return HttpSink(std::move(aSession));
}

template <typename Body = http::string_body>
using HttpBroadCastingSink =
    SinkGroup<HttpExpected<http::response<Body>>,
              HttpSink<HttpSession<AsyncSslStream, Body>>,
              HttpSink<HttpSession<AsyncTcpStream, Body>>>;

template <typename Body, typename... Args>
inline auto createHttpBroadCaster(Args&&... args)
{
    return HttpBroadCastingSink<Body>(
        HttpBroadCastingSink<http::string_body>::Sinks{
            (std::forward<Args>(args), ...)});
}
template <typename... Args>
inline auto createStringBodyBroadCaster(Args&&... args)
{
    return createHttpBroadCaster<http::string_body>(
        std::forward<Args>(args)...);
}

struct WebClient
{};
} // namespace reactor
