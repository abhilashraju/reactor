// main.cpp
#include "bmcwebhook.h"
#include <dlfcn.h>
#include <iostream>
#include <filesystem>
#include <memory>
namespace fs = std::filesystem;

int main() {
    fs::path lib_path_fs("/tmp/libredfish_event_plugin.so");
    std::string lib_path = lib_path_fs.string();

    // Load the library
    void* handle = dlopen(lib_path.c_str(), RTLD_LAZY);
    if (!handle) {
        std::cerr << "Cannot open library: " << dlerror() << '\n';
        return 1;
    }

    // Import a function from the library
    typedef std::shared_ptr<BmcWebHooks>(*func_t)();
    

    // reset errors
    dlerror();
    std::cout<< "Loading symbol create_plugin\n";
    func_t func = (func_t) dlsym(handle, "create_plugin");
    const char *dlsym_error = dlerror();
    if (dlsym_error) {
        std::cerr << "Cannot load symbol 'name_of_your_function': " << dlsym_error << '\n';
        dlclose(handle);
        return 1;
    }
    std::cout<< "Calling create_plugin\n";
    auto plugin = func();
    std::cout<< "Calling registerRoutes\n";
    // Call the function
    std::cout<< plugin->registerRoutes()<<"\n";

    // Close the library
    dlclose(handle);
}
// #include <iostream>
// #include <boost/dll/import.hpp> // for import_alias
// namespace fs = std::filesystem;
// namespace dll = boost::dll
// int main(int argc, char* argv[])
// {
//     std::cout << "Starting bmcwebhook" << std::endl;
    
//     // boost::dll::fs::path shared_library_path(
//     //     argv[1]); // argv[1] contains path to directory with our plugin library
//     // shared_library_path /= "redfish_event_plugin";
//     // typedef boost::shared_ptr<BmcWebHooks>(pluginapi_create_t)();
//     // std::function<pluginapi_create_t> creator;

//     // creator =
//     //     boost::dll::import_alias<pluginapi_create_t>( // type of imported symbol
//     //                                                   // must be explicitly
//     //                                                   // specified
//     //         shared_library_path,                      // path to library
//     //         "create_plugin",                          // symbol to import
//     //         boost::dll::load_mode::append_decorations // do append extensions
//     //                                                   // and prefixes
//     //     );

//     // boost::shared_ptr<BmcWebHooks> plugin = creator();
//     // plugin->registerRoutes();

//     return 0;
// }
