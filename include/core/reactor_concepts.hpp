#pragma once

#include <concepts>
namespace reactor
{
template <typename Handler, typename Arg>
concept MapFunction = requires(Handler h, Arg&& arg) {
                          {
                              h(std::forward<Arg>(arg))
                          }
                          -> std::same_as<std::invoke_result_t<Handler, Arg&&>>;
                      };
}
