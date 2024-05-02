#pragma once
#include "common/utilities.hpp"
#include "regex_range.hpp"

#include <numeric>
#include <string>
#include <vector>
namespace reactor
{
struct http_function
{
    struct parameter
    {
        std::string_view name;
        std::string_view value;
        parameter(std::string_view n, std::string_view v) :
            name(std::move(n)), value(std::move(v))
        {}
    };
    using parameters = std::vector<parameter>;
    std::string _name;
    parameters _params;
    const auto& name() const
    {
        return _name;
    }
    const auto& params() const
    {
        return _params;
    }
    auto& params()
    {
        return _params;
    }
    std::string operator[](const std::string& name) const
    {
        if (auto iter = std::find_if(begin(_params), end(_params),
                                     [&](auto& p) { return p.name == name; });
            iter != end(_params))
        {
            return {iter->value.data(), iter->value.size()};
        }
        return std::string();
    }
};
std::string to_string(std::string_view vw)
{
    return std::string(vw.data(), vw.length());
}
// inline std::vector<std::string> split(std::string_view sv, char delim) {
//   std::vector<std::string> res;
//   return std::accumulate(begin(sv), end(sv), res, [&](auto &sofar, auto c) {
//     if (sofar.size() == 0) {
//       sofar.emplace_back(std::string() + c);
//       return sofar;
//     }
//     if (c == delim) {
//       sofar.emplace_back();
//       return sofar;
//     }
//     sofar.back() += c;
//     return sofar;
//   });
// }
inline http_function parse_function(std::string_view target)
{
    auto index = target.find_last_of("/");
    if (index != std::string::npos)
    {
        auto func = target.substr(0, index);
        auto paramstring = target.substr(index, target.length() - index);
        auto funcindex = paramstring.find_first_of("?");
        if (funcindex != std::string::npos)
        {
            auto params =
                split(paramstring.substr(funcindex + 1,
                                         paramstring.length() - funcindex),
                      '&');
            http_function::parameters parampairs;
            for (auto& p : params)
            {
                auto pairs = split(p, '=');
                parampairs.emplace_back(pairs[0], pairs[1]);
            }
            return http_function{
                to_string(func) + to_string(paramstring.substr(0, funcindex)),
                std::move(parampairs)};
        }
        return http_function{to_string(target), http_function::parameters{}};
    }
    return http_function{};
}
void extract_params_from_path(http_function& func,
                              const std::string& handlerfuncname,
                              const std::string& pathfuncname)
{
    auto segs1 = split(handlerfuncname, '/', 1);
    auto segs2 = split(pathfuncname, '/', 1);
    if (segs1.size() != segs2.size())
        return;
    auto& params = func.params();
    std::transform(begin(segs1), end(segs1), begin(segs2),
                   std::back_inserter(params), [](auto& s1, auto& s2) {
        if (s1[0] == '{' && s1.back() == '}')
        {
            return http_function::parameter{s1.substr(1, s1.length() - 2), s2};
        }

        return http_function::parameter{s1, ""};
    });
}
// void extract_params_from_path(http_function& func,
//                               const std::string& handlerfuncname,
//                               const std::string& pathfuncname)
// {
//     auto regex = regex_range{handlerfuncname, std::regex(R"(\{(.*?)\})")};
//     auto sorrounds = regex.get_unmatched();
//     if (sorrounds.size() > 1)
//     {
//         auto names = regex.get_unmatched();

//         std::vector<std::pair<int, int>> endindices;
//         std::string functionname = pathfuncname;
//         endindices = std::accumulate(begin(names), end(names), endindices,
//                                      [&](auto sofar, auto v) {
//             int index = functionname.find(v);
//             functionname = functionname.substr(index + v.length());
//             if (sofar.size() < 1)
//             {
//                 sofar.emplace_back(index, index + v.length());
//                 return sofar;
//             }
//             index = sofar.back().second + index;
//             sofar.emplace_back(index, index + v.length());
//             return sofar;
//         });
//         std::vector<std::pair<int, int>> range;
//         std::adjacent_difference(begin(endindices), end(endindices),
//                                  std::back_inserter(range),
//                                  [](auto v1, auto v2) {
//             return std::make_pair(v2.second, v1.first);
//         });
//         std::vector<std::string> params;
//         std::transform(begin(range) + 1, end(range),
//         std::back_inserter(params),
//                        [&](auto v) {
//             return pathfuncname.substr(v.first, v.second - v.first);
//         });
//         auto variablenames = regex.get_matched();
//         if (params.size() == variablenames.size())
//         {
//             int i = 0;
//             while (i < params.size())
//             {
//                 func.params().emplace_back(variablenames[i], params[i]);
//                 i++;
//             }
//         }
//     }
// }
} // namespace reactor
