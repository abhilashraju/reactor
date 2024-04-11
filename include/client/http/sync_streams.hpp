#pragma once
#include "common/common_defs.hpp"
namespace reactor
{
template <typename Stream>
struct SyncStream : public std::enable_shared_from_this<SyncStream<Stream>>
{
  protected:
    Stream mStream;
    bool shutDownCalled{false};

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
        // Send the HTTP request to the remote host
        try
        {
            auto bytes_transferred = http::write(mStream, req, ec);
            if (ec)
            {
                return fail(ec, "write");
            }
            onWriteHandler(ec, bytes_transferred);
        }
        catch (const std::exception& e)
        {
            CLIENT_LOG_ERROR("Exception: {}", e.what());
            throw e;
        }
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
    TcpStream(TcpStream&& stream) : SyncStream(std::move(stream.mStream)){};
    void shutDown() override
    {
        shutDownCalled = true;
        lowestLayer().close();
    }
    TcpStream makeCopy()
    {
        return TcpStream(mStream.get_executor());
    }
};
#ifdef SSL_ON
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
    SslStream(SslStream&& stream) :
        SyncStream(std::move(stream.mStream)), sslCtx(stream.sslCtx){};
    void shutDown() override
    {
        shutDownCalled = true;
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
    SslStream makeCopy()
    {
        return SslStream(mStream.get_executor(), sslCtx);
    }
};
#endif
} // namespace reactor
