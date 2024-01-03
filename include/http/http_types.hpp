#pragma once
#include <boost/beast/http.hpp>

#include <string>
namespace reactor
{
struct Port
{
    std::string port;
    operator std::string() const
    {
        return port;
    }
};
struct Target
{
    std::string target;
    operator std::string() const
    {
        return target;
    }
};
struct Host
{
    std::string host;
    operator std::string() const
    {
        return host;
    }
};
struct Verb
{
    boost::beast::http::verb v;
    operator boost::beast::http::verb() const
    {
        return v;
    }
};
struct Version
{
    int v;
    operator int() const
    {
        return v;
    }
};
struct KeepAlive
{
    bool b;
    operator bool() const
    {
        return b;
    }
};
struct ContentType
{
    std::string type;
    operator std::string() const
    {
        return type;
    }
};
} // namespace reactor
