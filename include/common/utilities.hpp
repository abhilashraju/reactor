#pragma once
#include <ranges>
#include <string>
#include <vector>
namespace reactor
{
inline auto stringSplitter(char c, int skip = 0)
{
    return std::views::split(c) | std::views::drop(skip) |
           std::views::transform(
               [](auto&& sub) { return std::string(sub.begin(), sub.end()); });
}
inline auto split(const std::string_view& input, char c, int skip = 0)
{
    auto vw = input | stringSplitter(c, skip);
    return std::vector(vw.begin(), vw.end());
}
inline auto join(const auto& input, char c)
{
    std::string result;
    for (auto v : input)
    {
        result += c + v;
    }
    return result;
}
} // namespace reactor
