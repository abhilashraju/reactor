#pragma once
#include "http/http_client.hpp"

#include <boost/url/src.hpp>
#include <boost/url/url.hpp>
#include <boost/url/url_view.hpp>

#include <numeric>
#include <ranges>
namespace reactor
{
template <typename T>
struct FluxBase
{
    struct SourceHandler
    {
        virtual void next(std::function<void(T)> consumer) = 0;
        virtual bool hasNext() const = 0;
        virtual ~SourceHandler() {}
    };
    using CompletionToken = std::function<void(bool)>;

    using AsyncSubscriber = std::function<void(T, CompletionToken&&)>;
    using SyncSubscriber = std::function<void(T)>;

  protected:
    explicit FluxBase(SourceHandler* srcHandler) : mSource(srcHandler) {}
    std::unique_ptr<SourceHandler> mSource{};
    std::function<void()> onFinishHandler{};
    std::vector<std::function<T(T)>> mapHandlers{};
    std::variant<SyncSubscriber, AsyncSubscriber> subscriber;
    void invokeSubscriber(T& r, AsyncSubscriber& handler)
    {
        handler(std::move(r), [this](bool next) {
            if (next)
            {
                subscribe(std::move(std::get<AsyncSubscriber>(subscriber)));
            }
        });
    }
    void invokeSubscriber(T& r, SyncSubscriber& handler)
    {
        handler(std::move(r));
        subscribe(std::move(handler));
    }

  public:
    void subscribe(auto handler)
    {
        subscriber = std::move(handler);
        if (mSource->hasNext())
        {
            mSource->next([handler = std::move(handler), this](T v) {
                auto r = std::accumulate(begin(mapHandlers), end(mapHandlers),
                                         v, [](auto sofar, auto& func) {
                                             return func(std::move(sofar));
                                         });
                std::visit(
                    [&r, this](auto& handler) { invokeSubscriber(r, handler); },
                    subscriber);
            });

            return;
        }
        if (onFinishHandler)
        {
            onFinishHandler();
        }
    }
    FluxBase& onFinish(std::function<void()> finishH)
    {
        onFinishHandler = std::move(finishH);
        return *this;
    }
    FluxBase& map(std::function<T(T)> mapFun)
    {
        mapHandlers.push_back(std::move(mapFun));
        return *this;
    }
};

template <typename T, typename Session>
struct HttpSource : FluxBase<T>::SourceHandler
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
    void next(std::function<void(T)> consumer) override
    {
        decrement();
        session->setResponseHandler(
            [consumer = std::move(consumer)](
                const http::response<http::string_body>& res) {
            consumer(res.body());
        });
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

template <typename T,
          typename Session = HttpSession<AsyncSslStream, http::string_body>>
struct HttpSink
{
    using Response = http::response<typename Session::ResponseBody>;
    using ResponseHandler = std::function<void(const Response&, bool&)>;
    std::shared_ptr<Session> session;
    std::string url;
    ResponseHandler onDataHandler;
    explicit HttpSink(std::shared_ptr<Session> aSession) :
        session(std::move(aSession))
    {}
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

    void operator()(T data, auto&& requestNext)
    {
        session->setResponseHandler(
            [this, requestNext = std::move(requestNext)](const Response& res) {
            bool neednext{false};
            if (onDataHandler)
            {
                onDataHandler(res, neednext);
            }
            requestNext(neednext);
        });
        boost::urls::url_view urlvw(url);
        std::string h = urlvw.host();
        std::string p = urlvw.port();
        std::string path = urlvw.path();
        http::string_body::value_type body(std::move(data));

        session->setOptions(Host{h}, Port{p}, Target{path}, Version{11},
                            Verb{http::verb::post}, KeepAlive{true}, body,
                            ContentType{"plain/text"});
        session->run();
    }
};

template <typename Session>
HttpSource(std::shared_ptr<Session>) -> HttpSource<std::string, Session>;
template <typename T>
struct Mono : FluxBase<T>
{
    using Base = FluxBase<T>;
    struct Just : Base::SourceHandler
    {
        T value{};
        bool mHasNext{true};
        explicit Just(T v) : value(std::move(v)) {}
        void next(std::function<void(T)> consumer) override
        {
            mHasNext = false;
            consumer(std::move(value));
        }
        bool hasNext() const override
        {
            return mHasNext;
        }
    };

    explicit Mono(Base::SourceHandler* srcHandler) : Base(srcHandler) {}

    static Mono just(T v)
    {
        return Mono{new Just(std::move(v))};
    }
    template <typename Stream>
    static Mono connect(std::shared_ptr<HttpSession<Stream>> session,
                        const std::string& url)
    {
        auto src = new HttpSource<T, HttpSession<Stream>>(session, 1);
        src->setUrl(url);
        src->setVerb(http::verb::get);
        auto m = Mono{src};
        return m;
    }
};

template <typename T>
struct Flux : FluxBase<T>
{
    using Base = FluxBase<T>;
    template <class R>
    struct Range : Base::SourceHandler
    {
        R range{};
        R::iterator current{};
        explicit Range(R v) : range(std::move(v)), current(range.begin()) {}
        void next(std::function<void(T)> consumer) override
        {
            T v = std::move(*current);
            ++current;
            consumer(std::move(v));
        }
        bool hasNext() const override
        {
            return current != range.end();
        }
    };

    explicit Flux(Base::SourceHandler* srcHandler) : Base(srcHandler) {}
    template <class R>
    static Flux range(R v)
    {
        return Flux{new Range<R>(std::move(v))};
    }

    template <typename Stream>
    static Flux connect(std::shared_ptr<HttpSession<Stream>> session,
                        const std::string& url)
    {
        auto src = new HttpSource<T, HttpSession<Stream>>(session, 1, true);
        src->setUrl(url);
        src->setVerb(http::verb::get);
        auto m = Flux{src};
        return m;
    }
};

struct WebClient
{};
} // namespace reactor
