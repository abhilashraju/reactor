#pragma once
#include <algorithm>
#include <map>
#include <string>

inline auto parseCommandline(int argc, const char* argv[])
{
    int i = 1;
    std::map<std::string_view, std::string_view> comline;
    while (argc >= 3)
    {
        comline[argv[i]] = argv[i + 1];
        i += 2;
        argc -= 2;
    }
    // for (auto& p : comline)
    // {
    //     std::cout << p.first << " : " << p.second << "\n";
    // }
    return comline;
}
template <typename... Args>
inline auto getArgs(const auto& commLine, Args... args)
{
    auto extact = [&](auto a) {
        auto iter = std::find_if(begin(commLine), end(commLine),
                                 [&](auto v) { return v.first == a; });
        if (iter != end(commLine))
        {
            return iter->second;
        }
        using Type = decltype(iter->second);
        return Type{};
    };
    return std::make_tuple((extact(args))...);
}
