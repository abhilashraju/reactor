#pragma once
#include "core/reactor.hpp"
#include "http/http_client.hpp"

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
    int count{1};
    bool forever{false};
    explicit HttpSource(std::shared_ptr<Session> aSession, int shots,
                        bool infinite = false) :
        session(std::move(aSession)),
        count(shots), forever(infinite)
    {}
    void setUrl(std::string u)
    {
        boost::urls::url_view urlvw(u);
        std::string h = urlvw.host();
        std::string p = urlvw.port();
        std::string path = urlvw.path();
        session->setOptions(Host{h}, Port{p}, Target{path}, Version{11});
    }
    void setVerb(http::verb v)
    {
        session->setOptions(Verb{v});
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
        session->setResponseHandler(
            [consumer = std::move(consumer)](
                const Session::Request&, const Res& res) { consumer(res); });
        session->setOptions(KeepAlive{forever});
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
    template <typename Session>
    static HttpFluxBase connect(std::shared_ptr<Session> session,
                                const std::string& url,
                                http::verb v = http::verb::get)
    {
        auto src = new HttpSource<SourceType, Session>(session, 1, flux);
        src->setUrl(url);
        src->setVerb(v);
        auto m = HttpFluxBase{src};
        return m;
    }
    template <typename Session>
    static auto makeShared(std::shared_ptr<Session> session)
    {
        auto src = new HttpSource<SourceType, Session>(session, 1, flux);
        auto m = std::make_shared<HttpFluxBase>(src);
        return m;
    }
};
template <typename Body>
using HttpFlux = HttpFluxBase<Body, true>;
template <typename Body>
using HttpMono = HttpFluxBase<Body, false>;

template <typename SourceType,
          typename Session = HttpSession<AsyncSslStream, http::string_body>>
struct HttpSink
{
    using Response = Session::HttpExpected;

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

    void operator()(const SourceType& res, auto&& requestNext)
    {
        session->setResponseHandler(
            [this, requestNext = std::move(requestNext)](
                const Session::Request&, const Response& res) {
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
        using namespace std;
        typename Session::RequestBody::value_type body = tostring(res);

        session->setOptions(Host{h}, Port{p}, Target{path}, Version{11},
                            Verb{http::verb::post}, KeepAlive{true}, body,
                            ContentType{"plain/text"});
        session->run();
    }
    auto tostring(const auto& res)
    {
        using namespace std;
        return to_string(res);
    }
    auto tostring(const std::string& res)
    {
        return res;
    }
    // template <typename SourceResponse>
    // void fillBody(typename Session::RequestBody::value_type& body,
    //               const HttpExpected<SourceResponse>& res)
    // {
    //     body = res.response().body();
    // }
    // void fillBody(typename Session::RequestBody::value_type& body,
    //               const std::string& res)
    // {
    //     body = res;
    // }
};
template <typename SourceType, typename Session>
inline auto createHttpSink(std::shared_ptr<Session> aSession)
{
    return HttpSink<SourceType, Session>(std::move(aSession));
}

template <typename Body = http::string_body>
using HttpBroadCastingSink =
    AsyncSinkGroup<HttpExpected<http::response<Body>>,
                   HttpSink<HttpExpected<http::response<Body>>,
                            HttpSession<AsyncSslStream, Body>>,
                   HttpSink<HttpExpected<http::response<Body>>,
                            HttpSession<AsyncTcpStream, Body>>>;

template <typename Body, typename... Args>
inline auto createHttpBroadCaster(Args&&... args)
{
    using BroadCaster = HttpBroadCastingSink<Body>;
    using BroadCasterSinks = typename BroadCaster::Sinks;
    BroadCasterSinks vecSinks{std::forward<Args>(args)...};

    return BroadCaster(std::move(vecSinks));
}
template <typename... Args>
inline auto createStringBodyBroadCaster(Args&&... args)
{
    return createHttpBroadCaster<http::string_body>(
        std::forward<Args>(args)...);
}
template <typename Stream, typename ReqBody = http::empty_body,
          typename ResBody = http::string_body>
struct WebClient
{
    using Session = HttpSession<Stream, ReqBody, ResBody>;

  private:
    reactor::Host host;
    reactor::Port port;
    reactor::Target target;
    std::shared_ptr<Session> session;
    reactor::Verb verb;

  public:
    struct WebClientBuilder
    {
        std::string host;
        std::string port;
        std::string target;
        std::shared_ptr<Session> session;
        template <typename... Args>
        WebClientBuilder& withSession(auto& ex, Args&&... args)
        {
            session = Session::create(ex, std::forward<Args>(args)...);
            return *this;
        }
        WebClientBuilder& withEndpoint(const std::string& url)
        {
            boost::urls::url_view urlvw(url);
            host = urlvw.host();
            port = urlvw.port();
            target = urlvw.path();
            return *this;
        }

        WebClient create()
        {
            WebClient client;
            client.host = reactor::Host{host};
            client.port = reactor::Port{port};
            client.target = reactor::Target{target};
            client.session = session->clone();

            return client;
        }
    };
    static WebClientBuilder builder()
    {
        return WebClientBuilder();
    }
    WebClient& get()
    {
        verb = reactor::Verb{http::verb::get};
        return *this;
    }
    WebClient& post()
    {
        verb = reactor::Verb{http::verb::post};
        return *this;
    }
    WebClient& patch()
    {
        verb = reactor::Verb{http::verb::patch};
        return *this;
    }

    WebClient& put()
    {
        verb = reactor::Verb{http::verb::put};
        return *this;
    }

    WebClient& withBody(ReqBody::value_type body)
    {
        session->setOption(std::move(body));
        return *this;
    }
    WebClient& withContentType(ContentType type)
    {
        session->setOption(std::move(type));
        return *this;
    }

    std::shared_ptr<HttpFlux<ResBody>> toFlux()
    {
        auto m2 = HttpFlux<ResBody>::makeShared(std::move(session));
        return m2;
    }

    std::shared_ptr<HttpMono<ResBody>> toMono()
    {
        session->setOption(port);
        session->setOption(host);
        session->setOption(target);
        session->setOption(verb);
        auto m2 = HttpMono<ResBody>::makeShared(std::move(session));
        return m2;
    }
};
} // namespace reactor
