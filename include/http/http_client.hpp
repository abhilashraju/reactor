
#pragma once
#include "http_expected.hpp"
#include "logger/logger.hpp"

#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>

#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>

namespace reactor
{
namespace beast = boost::beast;   // from <boost/beast.hpp>
namespace http = beast::http;     // from <boost/beast/http.hpp>
namespace net = boost::asio;      // from <boost/asio.hpp>
namespace ssl = boost::asio::ssl; // from <boost/asio/ssl.hpp>
using tcp = boost::asio::ip::tcp; // from <boost/asio/ip/tcp.hpp>

//------------------------------------------------------------------------------

// Report a failure

template <typename Stream>
struct SyncStream : public std::enable_shared_from_this<SyncStream<Stream>>
{
  protected:
    Stream mStream;

  protected:
    using ErrorHandler = std::function<void(beast::error_code, const char*)>;
    ErrorHandler fail;
    SyncStream(Stream&& strm) : mStream(std::move(strm)) {}
    ~SyncStream()
    {
        CLIENT_LOG_DEBUG("SyncStream destroyed");
    }
    auto& lowestLayer()
    {
        return beast::get_lowest_layer(mStream);
    }
    Stream& stream()
    {
        return mStream;
    }
    virtual void
        on_resolve(std::function<void(beast::error_code)> connectionHandler,
                   tcp::resolver::results_type results) = 0;

  public:
    void setErrorHandler(ErrorHandler handler)
    {
        fail = std::move(handler);
    }
    void resolve(tcp::resolver& resolver, const char* host, const char* port,
                 std::function<void(beast::error_code)> handler)
    {
        beast::error_code ec{};
        tcp::resolver::results_type result = resolver.resolve(host, port, ec);
        if (ec)
        {
            return fail(ec, "resolve");
        }
        on_resolve(std::move(handler), result);
    }
    virtual void shutDown() = 0;
    template <typename Body>
    void write(
        http::request<Body>& req,
        std::function<void(beast::error_code, std::size_t)> onWriteHandler)
    {
        beast::error_code ec{};
        // Send the HTTP request to the remote host
        auto bytes_transferred = http::write(mStream, req, ec);
        if (ec)
        {
            return fail(ec, "write");
        }
        onWriteHandler(ec, bytes_transferred);
    }
    void read(auto& buffer, auto& res,
              std::function<void(beast::error_code, std::size_t)> onReadHandler)
    {
        beast::error_code ec{};
        // Receive the HTTP response
        auto bytes_transferred = http::read(mStream, buffer, res, ec);
        if (ec)
        {
            return fail(ec, "read");
        }
        onReadHandler(ec, bytes_transferred);
    }
    void monitorForError() {}
};
struct TcpStream : public SyncStream<beast::tcp_stream>
{
  protected:
    void on_resolve(std::function<void(beast::error_code)> connectionHandler,
                    tcp::resolver::results_type results) override
    {
        // Set a timeout on the operation
        lowestLayer().expires_after(std::chrono::seconds(30));
        beast::error_code ec{};
        // Make the connection on the IP address we get from a lookup
        auto epType = lowestLayer().connect(results, ec);
        if (ec)
        {
            return fail(ec, "connect");
        }
        connectionHandler(ec);
    }

  public:
    TcpStream(net::any_io_executor ex) : SyncStream(beast::tcp_stream(ex)) {}

    void shutDown() override
    {
        lowestLayer().close();
    }
    TcpStream makeCopy()
    {
        return TcpStream(mStream.get_executor());
    }
};
struct SslStream : public SyncStream<beast::ssl_stream<beast::tcp_stream>>
{
  private:
    ssl::context& sslCtx;
    void on_resolve(std::function<void(beast::error_code)> connectionHandler,
                    tcp::resolver::results_type results) override
    {
        // Set a timeout on the operation
        lowestLayer().expires_after(std::chrono::seconds(30));
        beast::error_code ec{};
        // Make the connection on the IP address we get from a lookup
        auto epType = lowestLayer().connect(results, ec);
        if (ec)
        {
            return fail(ec, "connect");
        }
        handShake(std::move(connectionHandler));
    }
    void handShake(std::function<void(beast::error_code)>&& connectionHandler)
    {
        beast::error_code ec{};
        // Perform the SSL handshake
        stream().handshake(ssl::stream_base::client, ec);
        if (ec)
        {
            return fail(ec, "handshake");
        }
        connectionHandler(ec);
    }

  public:
    SslStream(net::any_io_executor ex, ssl::context& ctx) :
        SyncStream(beast::ssl_stream<beast::tcp_stream>(ex, ctx)), sslCtx(ctx)
    {}
    void shutDown() override
    {
        lowestLayer().expires_after(std::chrono::seconds(30));
        beast::error_code ec{};
        // Gracefully close the stream
        stream().shutdown(ec);
        if (ec == net::error::eof)
        {
            ec = {};
        }
        if (ec)
            return fail(ec, "shutdown");
        // If we get here then the connection is closed
        // gracefully
    }
    // SslStream makeCopy() { return SslStream(mStream.get_executor(), sslCtx);
    // }
};

template <typename Stream>
struct ASyncStream : public std::enable_shared_from_this<ASyncStream<Stream>>
{
    using Base = std::enable_shared_from_this<ASyncStream<Stream>>;

  protected:
    Stream mStream;

  protected:
    using ErrorHandler = std::function<void(beast::error_code, const char*)>;
    ErrorHandler fail;
    ASyncStream(Stream&& strm) : mStream(std::move(strm)) {}
    ~ASyncStream()
    {
        CLIENT_LOG_DEBUG("ASyncStream destroyed");
    }
    auto& lowestLayer()
    {
        return beast::get_lowest_layer(mStream);
    }
    Stream& stream()
    {
        return mStream;
    }
    void on_resolve(std::function<void(beast::error_code)> connectionHandler,
                    beast::error_code ec, tcp::resolver::results_type results)
    {
        CLIENT_LOG_INFO("on_resolve {} : {}", ec.message(), ec.value());
        if (ec)
            return fail(ec, "resolve");

        // Set a timeout on the operation
        lowestLayer().expires_after(std::chrono::seconds(30));

        // Make the connection on the IP address we get from a
        // lookup
        CLIENT_LOG_INFO("try async_connect");
        lowestLayer().async_connect(
            results, beast::bind_front_handler(&ASyncStream::on_connect,
                                               Base::shared_from_this(),
                                               std::move(connectionHandler)));
    }
    virtual void
        on_connect(std::function<void(beast::error_code)> connectionHandler,
                   beast::error_code ec,
                   tcp::resolver::results_type::endpoint_type) = 0;
    void on_write(
        std::function<void(beast::error_code, std::size_t)> onWriteHandler,
        beast::error_code ec, std::size_t bytes_transferred)
    {
        if (ec)
            return fail(ec, "write");
        onWriteHandler(ec, bytes_transferred);
    }

    void on_read(
        std::function<void(beast::error_code, std::size_t)> onReadHandler,
        beast::error_code ec, std::size_t bytes_transferred)
    {
        if (ec)
            return fail(ec, "read");
        onReadHandler(ec, bytes_transferred);
    }

  public:
    void setErrorHandler(ErrorHandler handler)
    {
        fail = std::move(handler);
    }
    void resolve(tcp::resolver& resolver, const char* host, const char* port,
                 std::function<void(beast::error_code)> handler)
    {
        resolver.async_resolve(
            host, port,
            beast::bind_front_handler(&ASyncStream::on_resolve,
                                      Base::shared_from_this(),
                                      std::move(handler)));
    }
    virtual void shutDown() = 0;
    template <typename Body>
    void write(
        http::request<Body>& req,
        std::function<void(beast::error_code, std::size_t)> onWriteHandler)
    {
        // Send the HTTP request to the remote host
        http::async_write(stream(), req,
                          std::bind_front(&ASyncStream::on_write,
                                          Base::shared_from_this(),
                                          std::move(onWriteHandler)));
    }
    void read(auto& buffer, auto& res,
              std::function<void(beast::error_code, std::size_t)> onReadHandler)
    {
        // Receive the HTTP response
        http::async_read(stream(), buffer, res,
                         std::bind_front(&ASyncStream::on_read,
                                         Base::shared_from_this(),
                                         std::move(onReadHandler)));
    }
    void monitorForError()
    {
        CLIENT_LOG_DEBUG("monitorForError");
        lowestLayer().socket().async_wait(
            boost::asio::ip::tcp::socket::wait_read,
            std::bind_front(&ASyncStream::on_error, Base::shared_from_this()));
    }
    void on_error(const boost::system::error_code& ec)
    {
        if (ec)
        {
            fail(ec, "idle wait");
        }
    }
};
struct AsyncTcpStream : public ASyncStream<beast::tcp_stream>
{
  private:
    void on_connect(std::function<void(beast::error_code)> connectionHandler,
                    beast::error_code ec,
                    tcp::resolver::results_type::endpoint_type) override
    {
        if (ec)
            return fail(ec, "connect");
        connectionHandler(ec);
    }

  public:
    AsyncTcpStream(net::any_io_executor ex) : ASyncStream(beast::tcp_stream(ex))
    {}
    void shutDown()
    {
        stream().close();
    }
    AsyncTcpStream makeCopy()
    {
        return AsyncTcpStream(mStream.get_executor());
    }
};
struct AsyncSslStream : public ASyncStream<beast::ssl_stream<beast::tcp_stream>>
{
  private:
    ssl::context& sslCtx;
    void on_connect(std::function<void(beast::error_code)> connectionHandler,
                    beast::error_code ec,
                    tcp::resolver::results_type::endpoint_type) override
    {
        CLIENT_LOG_INFO("on_connect {} : {}", ec.message(), ec.value());
        if (ec)
            return fail(ec, "connect");

        // Perform the SSL handshake
        CLIENT_LOG_INFO("try async_handshake");
        stream().async_handshake(
            ssl::stream_base::client,
            [thisp = Base::shared_from_this(),
             connHandler = std::move(connectionHandler)](beast::error_code ec) {
            static_cast<AsyncSslStream*>(thisp.get())
                ->on_handshake(std::move(connHandler), ec);
        });
    }
    void on_shutdown(beast::error_code ec)
    {
        CLIENT_LOG_INFO("on_shutdown {} : {}", ec.message(), ec.value());
        if (ec == net::error::eof)
        {
            ec = {};
        }
        if (ec)
        {
            CLIENT_LOG_INFO("shutdown failed");
        }
        lowestLayer().close();
    }
    void on_handshake(std::function<void(beast::error_code)> connectionHandler,
                      beast::error_code ec)
    {
        CLIENT_LOG_INFO("handshake{} : {}", ec.message(), ec.value());
        if (ec)
            return fail(ec, "handshake");
        connectionHandler(ec);
    }

  public:
    AsyncSslStream(net::any_io_executor ex, ssl::context& ctx) :
        ASyncStream(beast::ssl_stream<beast::tcp_stream>(ex, ctx)), sslCtx(ctx)
    {}

    void shutDown()
    {
        // Set a timeout on the operation
        beast::get_lowest_layer(stream()).expires_after(
            std::chrono::seconds(30));

        // // Gracefully close the stream
        stream().async_shutdown(
            [thisp = Base::shared_from_this()](beast::error_code ec) {
            static_cast<AsyncSslStream*>(thisp.get())->on_shutdown(ec);
        });
    }
    AsyncSslStream makeCopy()
    {
        return AsyncSslStream(mStream.get_executor(), sslCtx);
    }
};

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
    void setErrorHandler()
    {
        stream->setErrorHandler([this](beast::error_code ec, const char* what) {
            CLIENT_LOG_DEBUG("{} : {}", what, ec.message());
            if (responseHandler)
            {
                http::response<ResBody> res{http::status::not_found, 11};
                responseHandler(req_, HttpExpected{res, ec});
            }
            stream->shutDown();
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
    template <typename... Options>
    void setOptions(Options... opts)
    {
        (setOption(opts), ..., 1);
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
            //   responseHandler(req_, HttpExpected{std::move(res_),
            //   beast::error_code{}});
        }

        if (!keepAlive)
        {
            connectionState = std::monostate();
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
};
template <typename Response>
inline std::string to_string(const HttpExpected<Response>& myStruct)
{
    return myStruct.to_string();
}
template <typename ReqBody = http::empty_body,
          typename ResBody = http::string_body>
using AsyncSslSession = HttpSession<AsyncSslStream, ReqBody, ResBody>;

template <typename ReqBody = http::empty_body,
          typename ResBody = http::string_body>
using AsyncTcpSession = HttpSession<AsyncTcpStream, ReqBody, ResBody>;

template <typename ReqBody = http::empty_body,
          typename ResBody = http::string_body>
using SslSession = HttpSession<SslStream, ReqBody, ResBody>;
template <typename ReqBody = http::empty_body,
          typename ResBody = http::string_body>
using TcpSession = HttpSession<TcpStream, ReqBody, ResBody>;
} // namespace reactor
