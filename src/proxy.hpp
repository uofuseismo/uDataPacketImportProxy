#ifndef UDATA_PACKET_IMPORT_PROXY_PROXY_HPP
#define UDATA_PACKET_IMPORT_PROXY_PROXY_HPP
#include <vector>
#include <future>
#include <memory>
#include <spdlog/spdlog.h>
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
    Proxy(const ProxyOptions &options, std::shared_ptr<spdlog::logger> &logger);
    /// @brief Starts the proxy service.
    [[nodiscard]] std::vector<std::future<void>> start();
    /// @brief Stops the proxy service. 
    void stop();
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
