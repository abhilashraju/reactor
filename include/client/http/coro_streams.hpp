#pragma once
#include "common/common_defs.hpp"
#include "logger/logger.hpp"
namespace reactor
{
template <typename Stream>
struct CoroStream : public std::enable_shared_from_this<CoroStream<Stream>>
{
    using Base = std::enable_shared_from_this<CoroStream<Stream>>;

  protected:
    Stream mStream;
    bool shutDownCalled{false};
    std::optional<net::yield_context> yield;

  protected:
    using ErrorHandler = std::function<void(beast::error_code, const char*)>;
    ErrorHandler fail;
    CoroStream(Stream&& strm) : mStream(std::move(strm)) {}
    ~CoroStream()
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
        auto connectresult = lowestLayer().async_connect(results,
                                                         yield.value()[ec]);
        on_connect(std::move(connectionHandler), ec, connectresult);
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
        std::string shost(host);
        std::string sport(port);
        net::spawn(resolver.get_executor(),
                   [this, &resolver, self = Base::shared_from_this(), shost,
                    sport,
                    handler = std::move(handler)](net::yield_context yield) {
            beast::error_code ec{};
            self->yield = yield;
            auto results = resolver.async_resolve(shost, sport, yield[ec]);
            on_resolve(std::move(handler), ec, results);
        });
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
        beast::error_code ec{};
        auto bytesWritten = http::async_write(stream(), req, yield.value()[ec]);
        on_write(std::move(onWriteHandler), ec, bytesWritten);
    }
    void read(auto& buffer, auto& res,
              std::function<void(beast::error_code, std::size_t)> onReadHandler)
    {
        // Receive the HTTP response
        beast::error_code ec{};
        auto bytesRead = http::async_read(stream(), buffer, res,
                                          yield.value()[ec]);
        on_read(std::move(onReadHandler), ec, bytesRead);
    }
    void monitorForError() {}
    void on_error(const boost::system::error_code& ec)
    {
        if (ec)
        {
            fail(ec, "idle wait");
        }
    }
};
struct CoroTcpStream : public CoroStream<beast::tcp_stream>
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
    CoroTcpStream(net::any_io_executor ex) : CoroStream(beast::tcp_stream(ex))
    {}
    CoroTcpStream(CoroTcpStream&& stream) :
        CoroStream(std::move(stream.mStream)){};
    void shutDown()
    {
        shutDownCalled = true;
        stream().close();
    }
    CoroTcpStream makeCopy()
    {
        return CoroTcpStream(mStream.get_executor());
    }
};
#ifdef SSL_ON
struct CoroSslStream : public CoroStream<beast::ssl_stream<beast::tcp_stream>>
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
        stream().async_handshake(ssl::stream_base::client, yield.value()[ec]);
        on_handshake(std::move(connectionHandler), ec);
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
    CoroSslStream(net::any_io_executor ex, ssl::context& ctx) :
        CoroStream(beast::ssl_stream<beast::tcp_stream>(ex, ctx)), sslCtx(ctx)
    {}
    CoroSslStream(CoroSslStream&& stream) :
        CoroStream(std::move(stream.mStream)), sslCtx(stream.sslCtx){};
    void shutDown()
    {
        shutDownCalled = true;
        // Set a timeout on the operation
        beast::get_lowest_layer(stream()).expires_after(
            std::chrono::seconds(30));

        // // Gracefully close the stream
        beast::error_code ec{};
        stream().async_shutdown(yield.value()[ec]);
        on_shutdown(ec);
    }
    CoroSslStream makeCopy()
    {
        return CoroSslStream(mStream.get_executor(), sslCtx);
    }
};
#endif
} // namespace reactor
