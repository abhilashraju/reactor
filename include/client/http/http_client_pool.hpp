#pragma once
#include "http_client.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>

#include <algorithm> // Added for std::ranges::find
#include <deque>
#include <memory>
#include <ranges>

namespace reactor
{
template <typename Handler, typename Stream>
concept SessionInitialiser =
    requires(Handler h, std::shared_ptr<HttpSession<Stream>> session) {
        {
            h(session)
        } -> std::same_as<void>;
    };
template <typename Session>
class HttpClientPool
{
  public:
    explicit HttpClientPool(net::any_io_executor executor,
                            std::size_t pool_size) :
        context(executor),
        poolSize(pool_size)
    {}
    ~HttpClientPool()
    {
        while (pool.size() > 0)
        {
            auto session = pool.front();
            pool.pop_front();
            session->close();
        }

        CLIENT_LOG_DEBUG("HttpClientPool destroyed");
    }
    HttpClientPool& withPoolSize(std::size_t pool_size)
    {
        poolSize = pool_size;
        return *this;
    }
    template <typename... Args>
    std::shared_ptr<Session> acquire(auto intializer, Args&&... args)
    {
        // Check if there is an available session in the pool
        auto it = std::ranges::find_if(
            pool, [](const auto& s) { return !s->inUse(); });
        if (it != std::end(pool))
        {
            return *it;
        }
        if (pool.size() < poolSize)
        {
            pool.push_back(
                Session::create(context, std::forward<Args>(args)...));
            intializer(pool.back());
            return pool.back();
        }
        return std::shared_ptr<Session>();
    }

    void release(std::shared_ptr<Session> session)
    {
        auto it = std::ranges::find_if(
            pool, [&](const auto& s) { return s.get() == session.get(); });
        if (it != pool.end())
        {
            pool.erase(it);
        }
    }

  private:
    net::any_io_executor context;
    std::size_t poolSize;
    std::deque<std::shared_ptr<Session>> pool;
};

} // namespace reactor
