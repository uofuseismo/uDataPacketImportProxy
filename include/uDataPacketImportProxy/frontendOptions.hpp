#ifndef UDATA_PACKET_IMPORT_PROXY_FRONTEND_OPTIONS_HPP
#define UDATA_PACKET_IMPORT_PROXY_FRONTEND_OPTIONS_HPP
#include <memory>
namespace UDataPacketImportProxy
{
 class GRPCOptions;
}
namespace UDataPacketImportProxy
{
class FrontendOptions
{
public:
    /// @brief Constructor.
    FrontendOptions();

    /// @brief Sets the GRPC options.
    void setGRPCOptions(const GRPCOptions &options);
    /// @result The GRPC options.
    [[nodiscard]] GRPCOptions getGRPCOptions() const noexcept;

    /// @brief Sets the maximum inbound message size in bytes.
    void setMaximumMessageSizeInBytes(int maxMessageSize);
    /// @result The max message size a producer can send.
    /// @note This is about 8 kb.
    [[nodiscard]] int getMaximumMessageSizeInBytes() const noexcept; 

    /// @brief Sets the maximum number of publishers.
    void setMaximumNumberOfPublishers(int maxPublishers);
    /// @result The maximum number of publishers.
    [[nodiscard]] int getMaximumNumberOfPublishers() const noexcept;

    /// @brief Sets the maximum number of consecutive invalid messages.
    ///        After this point, the publisher is popped.
    void setMaximumNumberOfConsecutiveInvalidMessages(int maxInvalidMessages);
    /// @result If a client sends this many conseuctive messages then it
    ///         will be popped.
    [[nodiscard]] int getMaximumNumberOfConsecutiveInvalidMessages() const noexcept;

    /// @brief Destructor.
    ~FrontendOptions();
    /// @brief Copy constructor.
    FrontendOptions(const FrontendOptions &options);
    /// @brief Move constructor.
    FrontendOptions(FrontendOptions &&options) noexcept;
    /// @brief Copy assignment.
    FrontendOptions& operator=(const FrontendOptions &options);
    /// @brief Move assignment.
    FrontendOptions& operator=(FrontendOptions &&options) noexcept;
private:
    class FrontendOptionsImpl;
    std::unique_ptr<FrontendOptionsImpl> pImpl;
};
}
#endif
