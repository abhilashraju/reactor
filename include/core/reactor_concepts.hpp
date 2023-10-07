#pragma once

#include <concepts>
#include <functional>
namespace reactor
{
template <typename Handler, typename Arg>
concept MapFunction = requires(Handler h, Arg&& arg) {
                          {
                              h(std::forward<Arg>(arg))
                          }
                          -> std::same_as<std::invoke_result_t<Handler, Arg&&>>;
                      };
template <typename Handler, typename Arg>
concept FilterFunction = requires(Handler h, Arg&& arg) {
                             {
                                 h(std::forward<Arg>(arg))
                             } -> std::same_as<bool>;
                         };

using CompletionToken = std::function<void(bool)>;
template <typename Handler, typename Arg>
concept SyncSubScribeFunction = requires(Handler h, const Arg& arg) {
                                    {
                                        h(arg)
                                    } -> std::same_as<void>;
                                };
template <typename Handler, typename Arg>
concept AsyncSubScribeFunction =
    requires(Handler h, const Arg& arg, CompletionToken&& token) {
        {
            h(arg, std::move(token))
        } -> std::same_as<void>;
    };
} // namespace reactor
