#include "bmcwebhook.h"

#include <boost/dll/alias.hpp> // for BOOST_DLL_ALIAS

#include <iostream>
#include <memory>
namespace BmcWeb
{

class MyBmcHook : public BmcWebHooks
{
    MyBmcHook() {}

  public:
    std::string name() const
    {
        return "aggregator";
    }
    std::string registerRoutes()
    {
        return "Registering aggregator routes";
    }

    // Factory method
    static std::shared_ptr<BmcWebHooks> create()
    {
        return std::shared_ptr<BmcWebHooks>(new MyBmcHook());
    }
};
#define BMCWEB_SYMBOL_EXPORT extern "C" __attribute__((visibility("default")))

#define BMCWEB_PLUGIN_ALIAS(FunctionOrVar, AliasName)                          \
    BMCWEB_SYMBOL_EXPORT const void* AliasName;                                \
    const void* AliasName = reinterpret_cast<const void*>(                     \
        reinterpret_cast<intptr_t>(&FunctionOrVar));

BMCWEB_PLUGIN_ALIAS(BmcWeb::MyBmcHook::create, // <-- this function is
                                               // exported with...
                    create_plugin              // <-- ...this alias name
)

BMCWEB_SYMBOL_EXPORT std::shared_ptr<BmcWebHooks> create_object()
{
    return BmcWeb::MyBmcHook::create();
}

} // namespace BmcWeb
