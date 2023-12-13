#include "bmcwebhook.h"

#include <boost/dll/alias.hpp> // for BOOST_DLL_ALIAS

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
    void registerRoutes()
    {
        std::cout << "Registering aggregator routes" << std::endl;
    }

    // Factory method
    static std::shared_ptr<MyBmcHook> create()
    {
        return std::shared_ptr<MyBmcHook>(new MyBmcHook());
    }
};

BOOST_DLL_ALIAS(BmcWeb::MyBmcHook::create, // <-- this function is
                                           // exported with...
                create_plugin              // <-- ...this alias name
)

} // namespace BmcWeb
