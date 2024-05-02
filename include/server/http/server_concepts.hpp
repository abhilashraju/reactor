#pragma once
#include "common/reactor_concepts.hpp"
#include "server/http/http_target_parser.hpp"
namespace reactor
{
template <typename Handler>
concept DynBodyRequestHandler = requires(Handler h, const DynamicbodyRequest& r,
                                         const http_function& f,
                                         net::yield_context yield) {
                                    {
                                        h(r, f, yield)
                                    } -> std::same_as<VariantResponse>;
                                };
template <typename Handler>
concept StringBodyRequestHandler =
    requires(Handler h, const StringbodyRequest& r, const http_function& f,
             net::yield_context yield) {
        {
            h(r, f, yield)
        } -> std::same_as<VariantResponse>;
    };
} // namespace reactor
