#ifndef UDATA_PACKET_IMPORT_PROXY_PROXY_OPTIONS_HPP
#define UDATA_PACKET_IMPORT_PROXY_PROXY_OPTIONS_HPP
#include <memory>
#include <optional>
namespace UDataPacketImportProxy
{
 class BackendOptions;
 class FrontendOptions;
 class DuplicatePacketDetectorOptions;
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

    /// @brief Sets the duplicate packet detector options.
    /// @note This is useful when we expect a publisher to be scaled up
    ///       prior to being purged from the system.
    void setDuplicatePacketDetectorOptions(const DuplicatePacketDetectorOptions &options);
    /// @result The duplicate packet detector options.
    [[nodiscard]] std::optional<DuplicatePacketDetectorOptions> getDuplicatePacketDetectorOptions() const noexcept;

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
