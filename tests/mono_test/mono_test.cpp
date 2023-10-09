
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
    m.onFinish([&finished]() { finished = true; }).subscribe([](auto v) {
        EXPECT_EQ(v, 10);
    });
    EXPECT_EQ(finished, true);
}
TEST(mono, just_int_with_map)
{
    bool finished{false};
    auto m2 = Mono<std::string>::just(std::string("hi"));

    m2.map(
          [](const auto& v) {
        return v.length();
    }).map([](const auto& v) -> bool {
          return v >= 2;
      }).subscribe([](auto v) { EXPECT_EQ(v, true); });
    Mono<std::string>::just(std::string("h"))
        .map([](const auto& v) { return v.length(); })
        .map([](const auto& v) { return v >= 2; })
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
        auto m = mono->map(
                         [](const auto& v) {
            return v.length();
        }).map([](const auto& v) {
              return v >= 2;
          }).makeLazy();
        fun = [m = std::move(m)]() mutable {
            m.subscribe([](auto v) { EXPECT_EQ(v, false); });
        };
    }
    fun();
}
TEST(mono, generator)
{
    auto m2 = Mono<std::string>::justFrom([]() { return "generated"; });

    m2.map(
          [](const auto& v) {
        return v.length();
    }).map([](const auto& v) {
          return v >= 5;
      }).subscribe([](auto v, auto reqNext) {
        EXPECT_EQ(v, true);
        reqNext(true);
    });

    auto m3 = Mono<std::string>::justPtr([]() { return "generated"; });

    m3->map(
          [](const auto& v) {
        return v.length();
    }).map([](const auto& v) {
          return v >= 5;
      }).subscribe([](auto v, auto reqNext) {
        EXPECT_EQ(v, true);
        reqNext(true);
    });
}
TEST(mono, scope_test_sinkgroup)
{
    auto fun1 = [](bool v) { EXPECT_EQ(v, false); };
    auto fun2 = [](bool v) { EXPECT_EQ(v, false); };
    // using SinkType = std::function<void(bool)>;
    // SinkGroup<bool, SinkType> sinks{{std::move(fun1), std::move(fun2)}};
    auto mono = Mono<std::string>::justPtr(std::string("h"));
    mono->map(
            [](const auto& v) {
        return v.length();
    }).map([](const auto& v) {
          return v >= 2;
      }).subscribe(createSinkGroup<bool>(fun1, fun2));
}
