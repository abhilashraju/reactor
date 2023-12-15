// main.cpp
#include "bmcwebhook.h"

#include "share_library.hpp"

#include <filesystem>
#include <iostream>
#include <memory>

int main()
{
    try
    {
        std::filesystem::path lib_path_fs(
            "/Users/abhilashraju/work/cpp/reactor/build/examples/plugins/redfish_events/libredfish_event_plugin.dylib");

        BmcWeb::PluginLoader loader(lib_path_fs);
        auto plugin = loader.loadPlugin<BmcWebHooks>("create_object");
        BMCWEB_LOG_INFO("{}", plugin->registerRoutes());
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
    }
}
// #include "static_plugin.hpp" // without this headers some compilers may
// optimize out the `create_plugin` symbol

// #include <boost/dll/runtime_symbol_info.hpp> // for program_location()
// #include <boost/dll/shared_library.hpp>      // for shared_library
// #include <boost/function.hpp>

// #include <iostream>

// namespace dll = boost::dll;

// int main()
// {
//     dll::shared_library self(dll::program_location());

//     std::cout << "Call function" << std::endl;
//     boost::function<boost::shared_ptr<my_plugin_api>()> creator =
//         self.get_alias<boost::shared_ptr<my_plugin_api>()>("create_plugin");

//     std::cout << "Computed Value: " << creator()->calculate(2, 2) <<
//     std::endl;
// }
