
#pragma once
#include "http_types.hpp"

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
  private:
    Stream mStream;

  protected:
    using ErrorHandler = std::function<void(beast::error_code, const char*)>;
    ErrorHandler fail;
    SyncStream(Stream&& strm) : mStream(std::move(strm)) {}
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
};
struct SslStream : public SyncStream<beast::ssl_stream<beast::tcp_stream>>
{
  private:
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
        SyncStream(beast::ssl_stream<beast::tcp_stream>(ex, ctx))
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
};

template <typename Stream>
struct ASyncStream : public std::enable_shared_from_this<ASyncStream<Stream>>
{
    using Base = std::enable_shared_from_this<ASyncStream<Stream>>;

  private:
    Stream mStream;

  protected:
    using ErrorHandler = std::function<void(beast::error_code, const char*)>;
    ErrorHandler fail;
    ASyncStream(Stream&& strm) : mStream(std::move(strm)) {}
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
        if (ec)
            return fail(ec, "resolve");

        // Set a timeout on the operation
        lowestLayer().expires_after(std::chrono::seconds(30));

        // Make the connection on the IP address we get from a
        // lookup
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
};
struct ASyncTcpStream : public ASyncStream<beast::tcp_stream>
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
    ASyncTcpStream(net::any_io_executor ex) : ASyncStream(beast::tcp_stream(ex))
    {}
    void shutDown()
    {
        stream().close();
    }
};
struct AsyncSslStream : public ASyncStream<beast::ssl_stream<beast::tcp_stream>>
{
  private:
    void on_connect(std::function<void(beast::error_code)> connectionHandler,
                    beast::error_code ec,
                    tcp::resolver::results_type::endpoint_type) override
    {
        if (ec)
            return fail(ec, "connect");

        // Perform the SSL handshake
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
        if (ec == net::error::eof)
        {
            ec = {};
        }
        if (ec)
            return fail(ec, "shutdown");

        // If we get here then the connection is closed
        // gracefully
    }
    void on_handshake(std::function<void(beast::error_code)> connectionHandler,
                      beast::error_code ec)
    {
        if (ec)
            return fail(ec, "handshake");
        connectionHandler(ec);
    }

  public:
    AsyncSslStream(net::any_io_executor ex, ssl::context& ctx) :
        ASyncStream(beast::ssl_stream<beast::tcp_stream>(ex, ctx))
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
};
// Performs an HTTP GET and prints the response
template <typename Stream, typename ReqBody = http::empty_body,
          typename ResBody = http::string_body>
class HttpSession :
    public std::enable_shared_from_this<HttpSession<Stream, ReqBody, ResBody>>
{
    struct InUse
    {
        std::shared_ptr<HttpSession> session;
        InUse(std::shared_ptr<HttpSession> sess) : session(std::move(sess)) {}
        void resolve() {}
        void write() {}
        void read() {}
    };
    struct Idle
    {
        std::shared_ptr<HttpSession> session;
        Idle(std::shared_ptr<HttpSession> sess) : session(std::move(sess)) {}
        void resolve() {}
        void write()
        {
            session->connectionState = InUse(session->shared_from_this());
            session->stream->write(
                session->req_, std::bind_front(&HttpSession::on_write,
                                               session->shared_from_this()));
        }
        void read()
        {
            session->connectionState = InUse(session->shared_from_this());
            session->stream->read(session->buffer_, session->res_,
                                  std::bind_front(&HttpSession::on_read,
                                                  session->shared_from_this()));
        }
    };
    struct Disconnected
    {
        std::shared_ptr<HttpSession> session;
        Disconnected(std::shared_ptr<HttpSession> sess) :
            session(std::move(sess))
        {}
        void resolve()
        {
            session->connectionState = InUse(session->shared_from_this());
            session->stream->resolve(
                session->resolver_, session->host.data(), session->port.data(),
                std::bind_front(&HttpSession::on_connect,
                                session->shared_from_this()));
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
    http::request<ReqBody> req_;
    http::response<ResBody> res_;
    using ResponseHandler =
        std::function<void(const http::response<http::string_body>&)>;

    ResponseHandler resonseHandler;
    std::string host;
    std::string port;
    std::variant<std::monostate, InUse, Idle, Disconnected> connectionState;
    bool keepAlive{false};
    HttpSession(net::any_io_executor ex, Stream&& astream) :
        resolver_(ex), stream(std::make_shared<Stream>(std::move(astream)))
    {
        stream->setErrorHandler([this](beast::error_code ec, const char* what) {
            std::cerr << what << ": " << ec.message() << "\n";
            if (resonseHandler)
            {
                http::response<http::string_body> res{http::status::not_found,
                                                      11};
                res.body() = "Client Error";
                resonseHandler(res);
            }
        });
    }

  public:
    using ResponseBody = ResBody;
    using RequestBody = ReqBody;
    [[nodiscard]] static std::shared_ptr<HttpSession<Stream, ReqBody, ResBody>>
        create(net::any_io_executor ex, Stream&& astream)
    {
        return std::shared_ptr<HttpSession<Stream, ReqBody, ResBody>>(
            new HttpSession<Stream, ReqBody, ResBody>(ex, std::move(astream)));
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
    // Start the asynchronous operation
    void run()
    {
        if (std::holds_alternative<std::monostate>(connectionState))
        {
            req_.set(http::field::host, host);
            req_.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
            req_.keep_alive(keepAlive);
            connectionState = Disconnected(Base::shared_from_this());
            visit([](auto& state) { state.resolve(); });
            return;
        }
        visit([](auto& state) { state.write(); });
    }
    void on_connect(beast::error_code ec)
    {
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
        connectionState = Idle(Base::shared_from_this());
        read();
    }
    void read()
    {
        visit([](auto& state) { state.read(); });
    }
    void on_read(beast::error_code ec, std::size_t bytes_transferred)
    {
        if (resonseHandler)
        {
            resonseHandler(res_);
        }
        res_ = http::response<ResBody>{};
        if (!keepAlive)
        {
            connectionState = std::monostate();
            stream->shutDown();
            return;
        }
        res_.keep_alive(keepAlive);
        connectionState = Idle(Base::shared_from_this());
    }
    bool inUse() const
    {
        return std::holds_alternative<InUse>(connectionState);
    }
    void setResponseHandler(ResponseHandler handler)
    {
        resonseHandler = std::move(handler);
    }
};
} // namespace reactor
