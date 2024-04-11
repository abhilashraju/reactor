#pragma once
#include "beast_defs.hpp"

#include <boost/asio/spawn.hpp>

#include <iostream>
#ifdef SSL_ON
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/ssl.hpp>
#endif
#include "base64_file_body.hpp"

#include <boost/beast/version.hpp>
namespace reactor
{

using FilebodyBase64Response = http::response<base64_file_body>;
using FilebodyResponse = http::response<http::file_body>;
using StringbodyResponse = http::response<http::string_body>;
using DynamicbodyResponse = http::response<http::dynamic_body>;
using EmptybodyResponse = http::response<http::empty_body>;

using VariantResponse = std::variant<EmptybodyResponse, FilebodyResponse,
                                     StringbodyResponse, DynamicbodyResponse>;
using FilebodyRequest = http::request<http::file_body>;
using StringbodyRequest = http::request<http::string_body>;
using DynamicbodyRequest = http::request<http::dynamic_body>;
using VariantRequest =
    std::variant<FilebodyRequest, StringbodyRequest, DynamicbodyRequest>;

inline auto target(const VariantRequest& reqVariant)
{
    return std::visit([](auto&& req) { return req.target(); }, reqVariant);
}
inline http::verb method(const VariantRequest& reqVariant)
{
    return std::visit([](auto&& req) { return req.method(); }, reqVariant);
}
inline auto version(const VariantRequest& reqVariant)
{
    return std::visit([](auto&& req) { return req.version(); }, reqVariant);
}
template <typename ReqTye>
struct BodyHandlerImpl
{
    auto operator()(auto func) const
    {
        return [func = std::move(func)](
                   const auto& req, const auto& httpfunc) -> VariantResponse {
            return std::visit(
                [&](auto&& r) {
                using T = std::decay_t<decltype(r)>;
                if constexpr (std::is_same_v<ReqTye, T>)
                {
                    return func(r, httpfunc);
                }
                throw std::runtime_error("Requested Type miss match");
                return VariantResponse{};
            },
                req);
        };
    }
};
using DynamicBodyHandler = BodyHandlerImpl<DynamicbodyRequest>;
using StringBodyHandler = BodyHandlerImpl<StringbodyRequest>;

template <typename Moveonly>
struct CopyableMoveWrapper
{
    std::shared_ptr<Moveonly> moveOnly;
    CopyableMoveWrapper(Moveonly mv) : moveOnly(new Moveonly(std::move(mv))) {}
    Moveonly& get()
    {
        return *moveOnly;
    };
    Moveonly release()
    {
        Moveonly mv = std::move(*moveOnly.get());
        moveOnly.reset();
        return mv;
    }
};
} // namespace reactor
