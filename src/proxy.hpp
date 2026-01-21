#ifndef UDATA_PACKET_IMPORT_PROXY_HPP
#define UDATA_PACKET_IMPORT_PROXY_HPP
#include <memory>
namespace UDataPacketImportProxy
{
 class Frontend;
 class Backend;
}
namespace UDataPacketImportProxy
{
class Proxy
{
public:
    ~Proxy();
private:
    class ProxyImpl;
    std::unique_ptr<ProxyImpl> pImpl;
};
}
#endif
