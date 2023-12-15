#include "bmcwebhook.h"

#include <boost/dll/alias.hpp> // for BOOST_DLL_ALIAS
#include<memory>
#include <iostream>
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

BOOST_DLL_ALIAS(BmcWeb::MyBmcHook::create, // <-- this function is
                                           // exported with...
                create_plugin              // <-- ...this alias name
)

} // namespace BmcWeb
