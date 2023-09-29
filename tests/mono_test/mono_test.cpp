
#include "http/web_client.hpp"

#include <filesystem>
#include <fstream>
#include <map>

#include "gtest/gtest.h"
using namespace reactor;
TEST(mono, just_int)
{
    bool finished = false;
    auto m = Mono<int>::just(10);
    m.onFinish([&finished]() {
         finished = true;
     }).subscribe([](auto v, auto&& requestNext) {
        EXPECT_EQ(v, 10);
        requestNext(true);
    });
    EXPECT_EQ(finished, true);
}
TEST(mono, just_int_with_map)
{
    bool finished{false};
    auto m2 = Mono<std::string>::just(std::string("hi"));

    m2.map<int>(
          [](const auto& v) {
        return v.length();
    }).map<bool>([](const auto& v) {
          return v >= 2;
      }).subscribe([](auto v) { EXPECT_EQ(v, true); });
    Mono<std::string>::just(std::string("h"))
        .map<int>([](const auto& v) { return v.length(); })
        .map<bool>([](const auto& v) { return v >= 2; })
        .subscribe([](auto v) { EXPECT_EQ(v, false); });
}

TEST(mono, scope_test)
{
    Mono<std::string> m;
    {
        m = Mono<std::string>::just(std::string("h"));
    }
    m.subscribe([](auto v) { EXPECT_EQ(v, "h"); });
}
TEST(mono, scope_test_mapper)
{
    std::function<void()> fun;

    {
        auto mono = Mono<std::string>::justPtr(std::string("h"));
        auto m = mono->map<int>(
                         [](const auto& v) {
            return v.length();
        }).map<bool>([](const auto& v) {
              return v >= 2;
          }).makeLazy();
        fun = [m = std::move(m)]() mutable {
            m.subscribe([](auto v) { EXPECT_EQ(v, false); });
        };
    }
    fun();
}
