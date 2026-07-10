#ifndef UDATA_PACKET_IMPORT_PROXY_BACKEND_OPTIONS_HPP
#define UDATA_PACKET_IMPORT_PROXY_BACKEND_OPTIONS_HPP
#include <memory>
namespace UDataPacketImportProxy
{
 class GRPCOptions;
}
namespace UDataPacketImportProxy
{
class BackendOptions
{
public:
    /// @brief Constructor.
    BackendOptions();

    /// @brief Sets the GRPC options.
    void setGRPCOptions(const GRPCOptions &options);
    /// @result The GRPC options.
    [[nodiscard]] GRPCOptions getGRPCOptions() const noexcept;

    /// @brief Sets the maximum number of subscribers.
    void setMaximumNumberOfSubscribers(int maxSubscribers);
    /// @result The maximum number of subscribers.
    [[nodiscard]] int getMaximumNumberOfSubscribers() const noexcept;

    /// @brief Sets the queue capacity for any subscriber
    void setQueueCapacity(int capacity);
    /// @result The queue capacity.
    [[nodiscard]] int getQueueCapacity() const noexcept;

    /// @brief Destructor.
    ~BackendOptions();
    /// @brief Copy constructor.
    BackendOptions(const BackendOptions &options);
    /// @brief Move constructor.
    BackendOptions(BackendOptions &&options) noexcept;
    /// @brief Copy assignment.
    BackendOptions& operator=(const BackendOptions &options);
    /// @brief Move assignment.
    BackendOptions& operator=(BackendOptions &&options) noexcept;
private:
    class BackendOptionsImpl;
    std::unique_ptr<BackendOptionsImpl> pImpl;
};
}
#endif
