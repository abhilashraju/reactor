#pragma once
#include "client/http/http_client.hpp"
#include "client/http/retry_request.hpp"
#include "core/reactor.hpp"

#include <boost/url/url.hpp>
#include <boost/url/url_view.hpp>
#include <nlohmann/json.hpp>

#include <expected>
#include <numeric>
#include <ranges>
namespace reactor
{
template <typename Type, typename Res>
struct ResponseEntity
{
    const Res& res_;
    Type data_;
    const auto& getHeaders() const
    {
        return res_.base();
    }
    const Type& data() const
    {
        return data_;
    }
};
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
    auto getSession() const
    {
        return session;
    }
    void setSession(std::shared_ptr<Session> aSession)
    {
        session = std::move(aSession);
    }
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

template <typename Session, bool flux>
struct HttpFluxBase :
    FluxBase<HttpExpected<http::response<typename Session::ResponseBody>>>
{
    using Body = typename Session::ResponseBody;
    using SourceType = HttpExpected<http::response<Body>>;

    using HttpSource = HttpSource<SourceType, Session>;
    using Base = FluxBase<HttpExpected<http::response<Body>>>;
    using RetryRequest = RetryRequest<typename Session::Request>;
    RetryPolicy retryPolicy;
    explicit HttpFluxBase(Base::SourceHandler* srcHandler) : Base(srcHandler) {}

    static HttpFluxBase connect(std::shared_ptr<Session> session,
                                const std::string& url,
                                http::verb v = http::verb::get)
    {
        auto src = new HttpSource(session, 1, flux);
        src->setUrl(url);
        src->setVerb(v);
        auto m = HttpFluxBase{src};
        return m;
    }

    static auto makeShared(std::shared_ptr<Session> session)
    {
        auto src = new HttpSource(session, 1, flux);
        auto m = std::make_shared<HttpFluxBase>(src);
        return m;
    }
    void retry(int count)
    {
        retryPolicy = {.maxRetries = count, .retryCount = 0, .retryDelay = 15};
    }
    bool retryIfNeeded(auto&& req, auto&& reqNext, auto src)
    {
        if (retryPolicy.retryNeeded())
        {
            retryPolicy.incrementRetryCount();
            auto retryRequest = std::make_shared<RetryRequest>(
                std::move(req), retryPolicy, src->getSession()->get_executor());
            retryRequest->retryFunction =
                [retrySelf = std::weak_ptr<RetryRequest>(retryRequest),
                 reqNext = std::move(reqNext), src]() {
                if (auto retryRequest = retrySelf.lock())
                {
                    auto session = src->getSession()->clone();
                    session->setOption(retryRequest->req.base());
                    session->setOption(
                        Host{retryRequest->req.base()[http::field::host]});
                    session->setOption(Port{retryRequest->req.base()["port"]});
                    src->setSession(session);
                    reqNext(true);
                }
            };

            retryRequest->waitAndRetry();
            return true;
        }
        return false;
    }
    void subscribeWithRetry(auto handler)
    {
        Base::subscribe(
            [handler = std::move(handler),
             self = Base::shared_from_this()](auto v, auto reqNext) mutable {
            try
            {
                if (!v.isError())
                {
                    handler(v);
                    reqNext(false);
                    return;
                }
            }
            catch (const std::exception& e)
            {
                CLIENT_LOG_ERROR("Caught Application Error: {}", e.what());
                CLIENT_LOG_INFO("Attempting retry: {}", e.what());
            }
            HttpFluxBase* b = static_cast<HttpFluxBase*>(self.get());
            HttpSource* src = static_cast<HttpSource*>(b->source());
            if (b->retryIfNeeded(src->getSession()->takeRequest(),
                                 std::move(reqNext), src))
            {
                return;
            }
            reqNext(false);
        });
    }
    // void subscribe(auto handler)
    // {
    //     subscribeWithRetry(std::move(handler));
    // }
    // using Base::subscribe;
    template <typename Handler>
    void asJson(Handler h)
    {
        subscribeWithRetry([h = std::move(h)](auto& v) mutable {
            using Entity = ResponseEntity<nlohmann::json, http::response<Body>>;
            nlohmann::json resJson = nlohmann::json::parse(v.response().body(),
                                                           nullptr, false);
            if (!resJson.is_discarded())
            {
                Entity res{v.response(), std::move(resJson)};
                HttpExpected<Entity> entity{res, beast::error_code{}};
                h(entity);
                return;
            }

            Entity res{v.response(), nlohmann::json{}};
            HttpExpected<Entity> entity{res, beast::http::error::bad_value};
            h(entity);
        });
    }
};
template <typename Session>
using HttpFlux = HttpFluxBase<Session, true>;
template <typename Session>
using HttpMono = HttpFluxBase<Session, false>;

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
    using Request = typename Session::Request;
    using Response = typename Session::Response;

  private:
    reactor::Host host;
    reactor::Port port;
    reactor::Target target;
    int retryCount{0};
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
        WebClientBuilder& withSession(auto ex, Args&&... args)
        {
            session = Session::create(ex, std::forward<Args>(args)...);
            return *this;
        }
        WebClientBuilder& withEndpoint(const std::string& url)
        {
            return withEndpoint(boost::urls::url_view(url));
        }
        WebClientBuilder& withEndpoint(const char* url)
        {
            return withEndpoint(std::string(url));
        }
        WebClientBuilder& withEndpoint(boost::urls::url_view urlvw)
        {
            host = urlvw.host();
            port = urlvw.port();
            target = urlvw.path();
            return *this;
        }
        WebClientBuilder& withHost(std::string h)
        {
            host = std::move(h);
            return *this;
        }
        WebClientBuilder& withPort(std::string p)
        {
            port = std::move(p);
            return *this;
        }
        WebClientBuilder& withTarget(std::string t)
        {
            target = std::move(t);
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
    WebClient& withMethod(http::verb v)
    {
        verb = reactor::Verb(v);
        return *this;
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
    WebClient& del()
    {
        verb = reactor::Verb{http::verb::delete_};
        return *this;
    }
    WebClient& withHeaders(Headers headers)
    {
        session->setOption(std::move(headers));
        return *this;
    }
    WebClient& withHeaders(const Request::header_type& headers)
    {
        session->setOption(headers);
        return *this;
    }
    WebClient& withHeader(Header header)
    {
        session->setOption(std::move(header));
        return *this;
    }
    WebClient& withBody(ReqBody::value_type body)
    {
        session->setOption(std::move(body));
        return *this;
    }
    WebClient& withBody(nlohmann::json&& body)
    {
        session->setOption(typename ReqBody::value_type(body.dump()));
        return *this;
    }
    WebClient& withContentType(ContentType type)
    {
        session->setOption(std::move(type));
        return *this;
    }
    WebClient& withRequest(Session::Request req)
    {
        target = Target{req.target()};
        verb = Verb{req.method()};
        session->setOption(std::move(req));
        return *this;
    }
    WebClient& withRetry(int c)
    {
        retryCount = c;
        return *this;
    }

    std::shared_ptr<HttpFlux<Session>> toFlux()
    {
        session->setOption(port);
        session->setOption(host);
        session->setOption(target);
        session->setOption(verb);
        auto m2 = HttpFlux<Session>::makeShared(std::move(session));
        m2->retry(retryCount);
        return m2;
    }

    std::shared_ptr<HttpMono<Session>> toMono()
    {
        session->setOption(port);
        session->setOption(host);
        session->setOption(target);
        session->setOption(verb);
        auto m2 = HttpMono<Session>::makeShared(std::move(session));
        return m2;
    }
};
} // namespace reactor
