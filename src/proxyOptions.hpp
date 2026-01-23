#ifndef UDATA_PACKET_IMPORT_PROXY_PROXY_OPTIONS_HPP
#define UDATA_PACKET_IMPORT_PROXY_PROXY_OPTIONS_HPP
#include <memory>
namespace UDataPacketImportProxy
{
 class BackendOptions;
 class FrontendOptions;
}
namespace UDataPacketImportProxy
{
class ProxyOptions
{
public:
    /// @brief Constructor.
    ProxyOptions();

    /// @brief Sets the proxy's frontend options.
    void setFrontendOptions(const FrontendOptions &options);
    /// @result The frontend options.
    [[nodiscard]] FrontendOptions getFrontendOptions() const;
 
    /// @brief Sets the proxy's backend options.
    void setBackendOptions(const BackendOptions &options);
    /// @result The backend options.
    [[nodiscard]] BackendOptions getBackendOptions() const;

    /// @brief Sets the internal queue size.
    void setQueueCapacity(int queueCapacity);
    /// @result The maximum internal queue size.
    [[nodiscard]] int getQueueCapacity() const noexcept;

    /// @brief Destructor.
    ~ProxyOptions();
    /// @brief Copy constructor.
    ProxyOptions(const ProxyOptions &options);
    /// @brief Move constructor.
    ProxyOptions(ProxyOptions &&options) noexcept;
    /// @brief Copy assignment.
    ProxyOptions& operator=(const ProxyOptions &options);
    /// @brief Move assignment.
    ProxyOptions& operator=(ProxyOptions &&options) noexcept;
private:
    class ProxyOptionsImpl;
    std::unique_ptr<ProxyOptionsImpl> pImpl;
};
}
#endif
