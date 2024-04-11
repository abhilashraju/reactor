
#pragma once
#include "async_streams.hpp"
#include "http_expected.hpp"
#include "logger/logger.hpp"
#include "sync_streams.hpp"

#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>

#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>

namespace reactor
{

template <typename SockStream, typename ReqBody = http::empty_body,
          typename ResBody = http::string_body>
class HttpSession :
    public std::enable_shared_from_this<
        HttpSession<SockStream, ReqBody, ResBody>>
{
  public:
    using Response = http::response<ResBody>;
    using Stream = SockStream;
    using Request = http::request<ReqBody>;
    using HttpExpected = HttpExpected<Response>;

  private:
    struct InUse
    {
        std::weak_ptr<HttpSession> session;
        InUse(std::weak_ptr<HttpSession> sess) : session(std::move(sess)) {}
        void resolve() {}
        void write() {}
        void read() {}
    };
    struct Idle
    {
        std::weak_ptr<HttpSession> session;
        Idle(std::weak_ptr<HttpSession> sess) : session(std::move(sess)) {}
        void resolve() {}
        void write()
        {
            CLIENT_LOG_DEBUG("Idle write");
            auto sessionptr = session.lock();
            sessionptr->connectionState = InUse(session);
            sessionptr->stream->write(
                sessionptr->req_,
                std::bind_front(&HttpSession::on_write, sessionptr));
        }
        void read()
        {
            CLIENT_LOG_DEBUG("Idle read");
            auto sessionptr = session.lock();
            sessionptr->connectionState = InUse(session);
            sessionptr->stream->read(
                sessionptr->buffer_, sessionptr->res_,
                std::bind_front(&HttpSession::on_read, sessionptr));
        }
    };
    struct Disconnected
    {
        std::weak_ptr<HttpSession> session;
        Disconnected(std::weak_ptr<HttpSession> sess) : session(std::move(sess))
        {}
        void resolve()
        {
            CLIENT_LOG_DEBUG("Disconnected resolve");
            auto sessionptr = session.lock();
            sessionptr->connectionState = InUse(session);
            sessionptr->stream->resolve(
                sessionptr->resolver_, sessionptr->host.data(),
                sessionptr->port.data(),
                std::bind_front(&HttpSession::on_connect, sessionptr));
        }
        void write()
        {
            resolve();
        }
        void read()
        {
            resolve();
        }
    };

  private:
    using Base =
        std::enable_shared_from_this<HttpSession<Stream, ReqBody, ResBody>>;

    tcp::resolver resolver_;
    std::shared_ptr<Stream> stream;
    beast::flat_buffer buffer_; // (Must persist between reads)
    Request req_;
    Response res_;
    using ResponseHandler =
        std::function<void(const Request&, const HttpExpected&)>;

    ResponseHandler responseHandler;
    std::string host;
    std::string port;
    std::variant<std::monostate, InUse, Idle, Disconnected> connectionState;
    bool keepAlive{false};

    HttpSession(const HttpSession&) = delete;
    HttpSession(HttpSession&&) = delete;
    HttpSession& operator=(const HttpSession&) = delete;
    HttpSession& operator=(HttpSession&&) = delete;

  public:
    using ResponseBody = ResBody;
    using RequestBody = ReqBody;
    template <typename... Args>
    HttpSession(net::any_io_executor ex, Args&&... args) :
        resolver_(ex),
        stream(std::make_shared<Stream>(ex, std::forward<Args>(args)...))
    {}
    HttpSession(net::any_io_executor ex, Stream&& strm) :
        resolver_(ex), stream(std::make_shared<Stream>(std::move(strm)))
    {}
    ~HttpSession()
    {
        CLIENT_LOG_DEBUG("HttpSession destroyed");
    }
    auto get_executor()
    {
        return resolver_.get_executor();
    }
    void setErrorHandler()
    {
        stream->setErrorHandler([this](beast::error_code ec, const char* what) {
            CLIENT_LOG_DEBUG("{} : {}", what, ec.message());
            if (responseHandler)
            {
                http::response<ResBody> res{http::status::not_found, 11};
                responseHandler(req_, HttpExpected{res, ec});
            }
            if (stream && !stream->closed())
            {
                responseHandler = ResponseHandler{};
                stream->shutDown();
            }
        });
    }
    template <typename... Args>
    [[nodiscard]] static std::shared_ptr<HttpSession>
        create(net::any_io_executor ex, Args&&... args)
    {
        auto session =
            std::make_shared<HttpSession>(ex, std::forward<Args>(args)...);
        session->setErrorHandler();
        return session;
    }

    [[nodiscard]] static std::shared_ptr<HttpSession>
        create(net::any_io_executor ex, Stream&& strm)
    {
        auto session = std::make_shared<HttpSession>(ex, std::move(strm));
        session->setErrorHandler();
        return session;
    }

    void setOption(ReqBody::value_type body)
    {
        req_.body() = std::move(body);
        req_.prepare_payload();
    }
    void setOption(Target t)
    {
        req_.target(t.target.data());
    }
    void setOption(Port p)
    {
        port = p;
    }
    void setOption(Host h)
    {
        host = h;
    }
    void setOption(Verb v)
    {
        req_.method(v);
    }
    void setOption(const Headers& h)
    {
        for (auto& header : h)
        {
            req_.set(header.key, header.value);
        }
    }

    void setOption(const Header& h)
    {
        req_.set(h.key, h.value);
    }
    void setOption(Version v)
    {
        req_.version(v);
    }
    void setOption(KeepAlive k)
    {
        keepAlive = k;
    }
    void setOption(ContentType t)
    {
        req_.set(http::field::content_type, t.type.data());
    }
    void setOption(const Request::header_type& headers)
    {
        for (auto& header : headers)
        {
            if (header.name() != http::field::unknown)
            {
                req_.set(header.name(), header.value());
            }
        }
    }
    template <typename... Options>
    void setOptions(Options... opts)
    {
        (setOption(opts), ..., 1);
    }
    void setOption(Request req)
    {
        req_ = std::move(req);
    }
    void close()
    {
        stream->shutDown();
    }
    void visit(auto&& handler)
    {
        std::visit(
            [&](auto&& state) {
            using Type = std::decay_t<decltype(state)>;
            if constexpr (!std::is_same_v<std::monostate, Type>)
            {
                handler(state);
            }
        },
            connectionState);
    }
    void execute()
    {
        if (std::holds_alternative<std::monostate>(connectionState))
        {
            connectionState = Disconnected(Base::shared_from_this());
            visit([](auto& state) { state.resolve(); });
            return;
        }
        visit([](auto& state) { state.write(); });
    }
    void run(Request&& req)
    {
        req_ = std::move(req);
        req_.prepare_payload();
        execute();
    }
    void run()
    {
        req_.set(http::field::host, host);
        req_.set("port", port);
        req_.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
        req_.keep_alive(keepAlive);
        req_.prepare_payload();
        execute();
    }
    void on_connect(beast::error_code ec)
    {
        CLIENT_LOG_DEBUG("on_connect {} : {}", ec.message(), ec.value());
        connectionState = Idle(Base::shared_from_this());
        write();
        // stream->write(req_, std::bind_front(&HttpSession::on_write,
        //                                     Base::shared_from_this()));
    }
    void write()
    {
        visit([](auto& state) { state.write(); });
    }
    void on_write(beast::error_code ec, std::size_t bytes_transferred)
    {
        CLIENT_LOG_DEBUG("on_write {} : {}", ec.message(), ec.value());
        connectionState = Idle(Base::shared_from_this());
        read();
    }
    void read()
    {
        visit([](auto& state) { state.read(); });
    }
    void on_read(beast::error_code ec, std::size_t bytes_transferred)
    {
        CLIENT_LOG_DEBUG("on_read {} : {}", ec.message(), ec.value());
        connectionState = Idle(Base::shared_from_this());
        bool keepAlive = res_.keep_alive();
        if (responseHandler)
        {
            responseHandler(req_,
                            HttpExpected{std::move(res_), beast::error_code{}});
        }

        if (!keepAlive)
        {
            connectionState = std::monostate();
            responseHandler = ResponseHandler{};
            stream->shutDown();
            return;
        }
        stream->monitorForError();
    }
    bool inUse() const
    {
        return std::holds_alternative<InUse>(connectionState);
    }
    void setResponseHandler(ResponseHandler handler)
    {
        responseHandler = std::move(handler);
    }
    auto clone()
    {
        auto copystream = stream->makeCopy();
        return create(resolver_.get_executor(), std::move(copystream));
    }
    template <typename NewReqBody>
    auto cloneWithBodyType()
    {
        auto copystream = stream->makeCopy();
        return HttpSession<Stream, NewReqBody, ResBody>::create(
            resolver_.get_executor(), std::move(copystream));
    }
    [[nodiscard]] http::request<ReqBody>&& takeRequest()
    {
        return std::move(req_);
    }
    [[nodiscard]] http::request<ReqBody> copyRequest()
    {
        return req_;
    }
};
template <typename Response>
inline std::string to_string(const HttpExpected<Response>& myStruct)
{
    return myStruct.to_string();
}

template <typename ReqBody = http::empty_body,
          typename ResBody = http::string_body>
using AsyncTcpSession = HttpSession<AsyncTcpStream, ReqBody, ResBody>;

template <typename ReqBody = http::empty_body,
          typename ResBody = http::string_body>
using TcpSession = HttpSession<TcpStream, ReqBody, ResBody>;
#ifdef SSL_ON
template <typename ReqBody = http::empty_body,
          typename ResBody = http::string_body>
using SslSession = HttpSession<SslStream, ReqBody, ResBody>;
template <typename ReqBody = http::empty_body,
          typename ResBody = http::string_body>
using AsyncSslSession = HttpSession<AsyncSslStream, ReqBody, ResBody>;
#endif
} // namespace reactor
