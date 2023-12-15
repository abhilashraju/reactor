#pragma once
#include "logger/logger.hpp"

#include <dlfcn.h>

#include <filesystem>
#define BMCWEB_LOG_DEBUG(message, ...) REACTOR_LOG_DEBUG(message, ##__VA_ARGS__)
#define BMCWEB_LOG_INFO(message, ...) REACTOR_LOG_INFO(message, ##__VA_ARGS__)
#define BMCWEB_LOG_WARNING(message, ...)                                       \
    REACTOR_LOG_WARNING(message, ##__VA_ARGS__)
#define BMCWEB_LOG_ERROR(message, ...) REACTOR_LOG_ERROR(message, ##__VA_ARGS__)

namespace BmcWeb
{
struct SharedLibrary
{
    void* handle;
    SharedLibrary(const std::filesystem::path& path)
    {
        handle = dlopen(path.c_str(), RTLD_LAZY);
        if (!handle)
        {
            BMCWEB_LOG_ERROR("Cannot open library: {}", dlerror());
            return;
        }
    }
    ~SharedLibrary()
    {
        dlclose(handle);
    }

    template <typename T>
    T loadSymbol(const std::string& symbolName)
    {
        dlerror(); // Reset errors
        auto symbol = reinterpret_cast<T>(dlsym(handle, symbolName.c_str()));
        const char* dlsym_error = dlerror();
        if (dlsym_error)
        {
            BMCWEB_LOG_ERROR("Cannot load symbol {} : {} ", symbolName,
                             dlsym_error);
            throw std::runtime_error("Failed to load symbol");
        }
        return symbol;
    }
};

struct PluginLoader
{
    SharedLibrary lib;
    PluginLoader(const std::filesystem::path& path) : lib(path) {}
    template <typename T>
    std::shared_ptr<T> loadPlugin(std::string_view symbolName)
    {
        using FUNC = std::shared_ptr<T> (*)();
        auto func = lib.loadSymbol<FUNC>(symbolName.data());
        auto plugin = func();
        return plugin;
    }
};
} // namespace BmcWeb
