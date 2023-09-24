#include "http/http_client.hpp"
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
