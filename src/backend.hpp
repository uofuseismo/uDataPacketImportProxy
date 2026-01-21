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
    explicit Backend(const BackendOptions &options);
    ~Backend();

    void stop();
    void start();
    void enqueuePacket(UDataPacketImportAPI::V1::Packet &&packet);

    Backend() = delete;
    Backend(const Backend &) = delete;
    Backend(Backend &&) noexcept = delete;
private:
    class BackendImpl;
    std::unique_ptr<BackendImpl> pImpl;
};
}
#endif
