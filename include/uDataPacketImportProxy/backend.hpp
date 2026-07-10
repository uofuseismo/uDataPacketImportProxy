#ifndef UDATA_PACKET_IMPORT_PROXY_BACKEND_HPP
#define UDATA_PACKET_IMPORT_PROXY_BACKEND_HPP
#include <memory>
namespace UDataPacketImportAPI::V1
{
 class Packet;
}
namespace UDataPacketImportProxy
{
 class BackendOptions;
}
namespace UDataPacketImportProxy
{
class Backend
{
public:
    Backend(const BackendOptions &options,
            std::shared_ptr<spdlog::logger> logger);
    ~Backend();

    void stop();
    void start();
    /// @brief Enqueues the next packet.
    /// @result The number of packets overwritten in the outbound queue.
    ///         This should be zero.
    [[nodiscard]] int enqueuePacket(UDataPacketImportAPI::V1::Packet &&packet);
    [[nodiscard]] int getNumberOfSubscribers() const;
    [[nodiscard]] bool isRunning() const noexcept;

    Backend() = delete;
    Backend(const Backend &) = delete;
    Backend(Backend &&) noexcept = delete;
private:
    class BackendImpl;
    std::unique_ptr<BackendImpl> pImpl;
};
}
#endif
