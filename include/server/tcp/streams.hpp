#pragma once
// #include "chai_concepts.hpp"
#include "common/common_defs.hpp"
// #include "handle_error.hpp"
#include "ssl/ssl_utils.hpp"
namespace reactor
{
inline auto checkFailed(beast::error_code& ec)
{
    if (ec)
    {
        std::cout << ec.message() << "\n";
        return true;
    }
    return false;
}
struct TCPStreamMaker
{
    struct SocketStreamReader
    {
        std::unique_ptr<net::io_context> ctx{
            std::make_unique<net::io_context>()};
        tcp::socket stream_{*ctx};
        SocketStreamReader() {}
        SocketStreamReader(const SocketStreamReader&) = delete;
        SocketStreamReader(SocketStreamReader&&) = default;

        void close(beast::error_code& ec)
        {
            stream_.close();
        }
        tcp::socket& stream()
        {
            return stream_;
        }
    };

    void acceptAsyncConnection(net::io_context& ioContext,
                               tcp::acceptor& acceptor, auto work)
    {
        auto do_accept = [&ioContext, work, this,
                          &acceptor](auto accept, net::yield_context yield) {
            SocketStreamReader reader;
            beast::error_code ec{};
            acceptor.async_accept(reader.stream(), yield);
            if (checkFailed(ec))
            {
                net::spawn(&ioContext, std::bind_front(accept, accept));
                return;
            }

            auto do_work =
                [accept, &ioContext, work,
                 reader = std::move(reader)](net::yield_context yield) mutable {
                work(std::move(reader));
                net::spawn(&ioContext, std::bind_front(accept, accept));
            };
            net::spawn(ioContext, std::move(do_work));
        };

        net::spawn(ioContext, std::bind_front(do_accept, do_accept));
    }
};
#ifdef SSL_ON
template <bool MTLS>
struct SslStreamMakerImpl
{
    struct SSlStreamReader
    {
        using StreamType = ssl::stream<tcp::socket>;
        std::shared_ptr<StreamType> stream_;

        SSlStreamReader(net::io_context& ioctx, ssl::context& sslContext) :
            stream_(std::make_shared<StreamType>(ioctx, sslContext))
        {}
        ~SSlStreamReader() {}
        void close(beast::error_code& ec)
        {
            stream_->shutdown(ec);
            stream_->next_layer().close();
        }
        ssl::stream<tcp::socket>& stream()
        {
            return *stream_;
        }
        const auto& endPoint() const
        {
            return stream_->next_layer().remote_endpoint();
        }
    };
    ssl::context sslContext;
    SslStreamMakerImpl(std::string_view cirtDir,
                       std::string_view trustStorePath = "") :
        sslContext(
            ensuressl::loadCertificate(boost::asio::ssl::context::tls_server,
                                       {cirtDir.data(), cirtDir.size()}))
    {
        if constexpr (MTLS)
        {
            ensuressl::prepareMutualTls(
                sslContext, trustStorePath.empty() ? ensuressl::trustStorePath
                                                   : trustStorePath);
        }
    }

    void acceptAsyncConnection(net::io_context& ioContext,
                               tcp::acceptor& acceptor, auto work)
    {
        auto do_accept = [&ioContext, work, this,
                          &acceptor](auto accept, net::yield_context yield) {
            SSlStreamReader reader(ioContext, sslContext);

            beast::error_code ec{};
            acceptor.async_accept(reader.stream().next_layer(), yield[ec]);
            net::spawn(&ioContext, std::bind_front(accept, accept));
            if (checkFailed(ec))
            {
                return;
            }
            doHandshake(work, std::move(reader), yield);
        };
        net::spawn(ioContext, std::bind_front(do_accept, do_accept));
    }
    static void doHandshake(auto work, SSlStreamReader&& reader,
                            net::yield_context yield)
    {
        beast::error_code ec{};
        reader.stream().async_handshake(ssl::stream_base::server, yield[ec]);
        if (checkFailed(ec))
        {
            return;
        }
        work(std::move(reader), yield);
    }
};
using SslStreamMaker = SslStreamMakerImpl<false>;
using MtlsStreamMaker = SslStreamMakerImpl<true>;
#endif
} // namespace reactor
