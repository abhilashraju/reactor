// main.cpp
#include "bmcwebhook.h"

#include <boost/dll/import.hpp> // for import_alias

int main(int argc, char* argv[])
{
    boost::dll::fs::path shared_library_path(
        argv[1]); // argv[1] contains path to directory with our plugin library
    shared_library_path /= "redfish_event_plugin";
    typedef boost::shared_ptr<BmcWebHooks>(pluginapi_create_t)();
    std::function<pluginapi_create_t> creator;

    creator =
        boost::dll::import_alias<pluginapi_create_t>( // type of imported symbol
                                                      // must be explicitly
                                                      // specified
            shared_library_path,                      // path to library
            "create_plugin",                          // symbol to import
            boost::dll::load_mode::append_decorations // do append extensions
                                                      // and prefixes
        );

    boost::shared_ptr<BmcWebHooks> plugin = creator();
    plugin->registerRoutes();

    return 0;
}
