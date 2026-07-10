#ifndef UDATA_PACKET_IMPORT_PROXY_PROXY_HPP
#define UDATA_PACKET_IMPORT_PROXY_PROXY_HPP
#include <vector>
#include <future>
#include <memory>
#include <spdlog/spdlog.h>
import metrics;
namespace UDataPacketImportProxy
{
 class ProxyOptions;
}
namespace UDataPacketImportProxy
{
/// @class Proxy
/// @brief The proxy is an aggregation point.  Publishers send data packets
///        to the frontend and subscribers read packets from the backend.
/// @copyright Ben Baker (University of Utah) distributed under the
///            MIT NO AI license.
class Proxy
{
public:
    /// @brief Constructs the proxy.
    Proxy(const ProxyOptions &options, std::shared_ptr<spdlog::logger> logger);
    /// @brief Starts the proxy service.
    void start();
    /// @brief Stops the proxy service. 
    void stop();
    /// @result The number of packets received.
    [[nodiscard]] int64_t getNumberOfPacketsReceived() const noexcept;
    /// @result The number of frontend publishers
    [[nodiscard]] int getNumberOfPublishers() const noexcept;
    /// @reuslt The number of backend subscribers. 
    [[nodiscard]] int getNumberOfSubscribers() const noexcept;
    /// @brief Destructor.
    ~Proxy();

    Proxy() = delete;
    Proxy(const Proxy &) = delete;
    Proxy(Proxy &&) noexcept = delete;
    Proxy& operator=(const Proxy &) = delete;
    Proxy& operator=(Proxy &&) noexcept = delete;
private:
    class ProxyImpl;
    std::unique_ptr<ProxyImpl> pImpl;
};
}
#endif
