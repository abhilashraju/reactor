#pragma once
#include "common/reactor_concepts.hpp"
#include "server/http_target_parser.hpp"
namespace reactor
{
template <typename Handler>
concept DynBodyRequestHandler =
    requires(Handler h, const DynamicbodyRequest& r, const http_function& f) {
        {
            h(r, f)
        } -> std::same_as<VariantResponse>;
    };
template <typename Handler>
concept StringBodyRequestHandler =
    requires(Handler h, const StringbodyRequest& r, const http_function& f) {
        {
            h(r, f)
        } -> std::same_as<VariantResponse>;
    };
} // namespace reactor
