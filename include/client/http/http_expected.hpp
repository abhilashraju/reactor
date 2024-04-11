#pragma once
#include "http_types.hpp"

#include <boost/beast/core.hpp>
namespace reactor
{
namespace beast = boost::beast; // from <boost/beast.hpp>

template <typename Response>
struct HttpExpected
{
    Response resp;
    beast::error_code ec{};
    bool isError() const
    {
        return ec.operator bool();
    }
    auto error() const
    {
        return ec;
    }
    const auto& response() const
    {
        return resp;
    }
    std::string to_string() const
    {
        return resp.body();
    }
};
} // namespace reactor
