#pragma once
#include "common/common_defs.hpp"
#include "logger/logger.hpp"
namespace reactor
{
template <typename Stream>
struct ASyncStream : public std::enable_shared_from_this<ASyncStream<Stream>>
{
    using Base = std::enable_shared_from_this<ASyncStream<Stream>>;

  protected:
    Stream mStream;
    bool shutDownCalled{false};

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
    bool closed() const
    {
        return shutDownCalled;
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
    AsyncTcpStream(AsyncTcpStream&& stream) :
        ASyncStream(std::move(stream.mStream)){};
    void shutDown()
    {
        shutDownCalled = true;
        stream().close();
    }
    AsyncTcpStream makeCopy()
    {
        return AsyncTcpStream(mStream.get_executor());
    }
};
#ifdef SSL_ON
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
    AsyncSslStream(AsyncSslStream&& stream) :
        ASyncStream(std::move(stream.mStream)), sslCtx(stream.sslCtx){};
    void shutDown()
    {
        shutDownCalled = true;
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
#endif
} // namespace reactor
