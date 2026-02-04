module;
#include <chrono>
export module getNow;

namespace UDataPacketImportProxy::Utilities
{
export std::chrono::microseconds getNow() 
{
     auto now 
        = std::chrono::duration_cast<std::chrono::microseconds>
          ((std::chrono::high_resolution_clock::now()).time_since_epoch());
     return now;
}
}
