#pragma once
#include <numeric>
#include <regex>
namespace reactor
{
template <typename Range>
struct regex_range
{
    const Range& str;
    std::regex regex;
    regex_range(const Range& s, std::regex reg) : str(s), regex(std::move(reg))
    {}
    auto begin() const
    {
        return std::sregex_iterator(str.begin(), str.end(), regex);
    }
    auto end() const
    {
        return std::sregex_iterator();
    }
    std::vector<int> match_indices() const
    {
        std::vector<int> indices;
        return std::accumulate(begin(), end(),
                               std::pair<int, std::vector<int>>{0, indices},
                               [](auto sofar, const auto& val) {
            if (sofar.second.size() > 0)
            {
                sofar.second.push_back(sofar.second.back() + sofar.first +
                                       val.prefix().length());
                sofar.first = val.str().length();
                return sofar;
            }
            sofar.first = val.str().length();
            sofar.second.push_back(val.prefix().length());
            return sofar;
        }).second;
    }
    std::vector<std::string> get_matched() const
    {
        std::vector<std::string> out;
        copy_matched(std::back_inserter(out));
        return out;
    }
    std::vector<std::string> get_unmatched() const
    {
        std::vector<std::string> out;
        copy_unmatched(std::back_inserter(out));
        return out;
    }
    void copy_unmatched(auto outiter) const
    {
        std::copy(std::sregex_token_iterator(str.begin(), str.end(), regex, -1),
                  std::sregex_token_iterator(), outiter);
    }
    void copy_matched(auto outiter) const
    {
        std::copy(std::sregex_token_iterator(str.begin(), str.end(), regex, 1),
                  std::sregex_token_iterator(), outiter);
    }
};
} // namespace reactor
