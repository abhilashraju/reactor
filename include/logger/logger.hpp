#include <format>
#include <iostream>
#include <string>
namespace reactor
{
enum class LogLevel
{
    DEBUG,
    INFO,
    WARNING,
    ERROR
};
template <typename OutputStream>
class Logger
{
  public:
    Logger(LogLevel level, OutputStream& outputStream) :
        currentLogLevel(level), output(outputStream)
    {}

    void log(const char* filename, int lineNumber, LogLevel level,
             const std::string& message) const
    {
        if (isLogLevelEnabled(level))
        {
            output << std::format("{}:{} ", filename, lineNumber) << message
                   << "\n";
        }
    }

    void setLogLevel(LogLevel level)
    {
        currentLogLevel = level;
    }

  private:
    LogLevel currentLogLevel;
    OutputStream& output;

    bool isLogLevelEnabled(LogLevel level) const
    {
        return level >= currentLogLevel;
    }
};

inline Logger<std::ostream>& getLogger()
{
    static Logger<std::ostream> logger(LogLevel::DEBUG, std::cout);
    return logger;
}
} // namespace reactor

// Macros for clients to use logger
#define REACTOR_LOG_DEBUG(message, ...)                                        \
    getLogger().log(__FILE__, __LINE__, LogLevel::DEBUG,                       \
                    std::format("{} :" message, "Debug", ##__VA_ARGS__))
#define REACTOR_LOG_INFO(message, ...)                                         \
    getLogger().log(__FILE__, __LINE__, LogLevel::INFO,                        \
                    std::format("{} :" message, "Info", ##__VA_ARGS__))
#define REACTOR_LOG_WARNING(message, ...)                                      \
    getLogger().log(__FILE__, __LINE__, LogLevel::WARNING,                     \
                    std::format("{} :" message, "Warning", ##__VA_ARGS__))
#define REACTOR_LOG_ERROR(message, ...)                                        \
    getLogger().log(__FILE__, __LINE__, LogLevel::ERROR,                       \
                    std::format("{} :" message, "Error", ##__VA_ARGS__))

#define CLIENT_LOG_DEBUG(message, ...) REACTOR_LOG_DEBUG(message, ##__VA_ARGS__)
#define CLIENT_LOG_INFO(message, ...) REACTOR_LOG_INFO(message, ##__VA_ARGS__)
#define CLIENT_LOG_WARNING(message, ...)                                       \
    REACTOR_LOG_WARNING(message, ##__VA_ARGS__)
#define CLIENT_LOG_ERROR(message, ...) REACTOR_LOG_ERROR(message, ##__VA_ARGS__)
