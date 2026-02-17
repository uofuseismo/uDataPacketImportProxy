#include "proxyOptions.hpp"
#include "frontendOptions.hpp"
#include "backendOptions.hpp"

using namespace UDataPacketImportProxy;

class ProxyOptions::ProxyOptionsImpl
{
public:
    FrontendOptions mFrontendOptions;
    BackendOptions mBackendOptions;
    int mQueueCapacity{8192};
    int mDeduplicateCircularBufferSize{32};
};

/// Constructor
ProxyOptions::ProxyOptions() :
    pImpl(std::make_unique<ProxyOptionsImpl> ()) 
{
}

/// Copy constructor
ProxyOptions::ProxyOptions(const ProxyOptions &options)
{
    *this = options;
}

/// Move constructor
ProxyOptions::ProxyOptions(ProxyOptions &&options) noexcept
{
    *this = std::move(options);
}

/// Copy assignment
ProxyOptions& ProxyOptions::operator=(const ProxyOptions &options)
{
    if (&options == this){return *this;}
    pImpl = std::make_unique<ProxyOptionsImpl> (*options.pImpl);
    return *this;
}

/// Move assignment
ProxyOptions& ProxyOptions::operator=(ProxyOptions &&options) noexcept
{
    if (&options == this){return *this;}
    pImpl = std::move(options.pImpl);
    return *this;
}

/// Destructor
ProxyOptions::~ProxyOptions() = default;

/// Frontend options
void ProxyOptions::setFrontendOptions(const FrontendOptions &options) 
{ 
    pImpl->mFrontendOptions = options;
}

FrontendOptions ProxyOptions::getFrontendOptions() const
{
    return pImpl->mFrontendOptions;
}

/// Backend options
void ProxyOptions::setBackendOptions(const BackendOptions &options)
{
    pImpl->mBackendOptions = options;
}

BackendOptions ProxyOptions::getBackendOptions() const
{
    return pImpl->mBackendOptions;
}

/// Maximum queue size
void ProxyOptions::setQueueCapacity(const int maxQueueCapacity)
{
    if (maxQueueCapacity < 1)
    {
        throw std::invalid_argument("Queue capacity must be positive");
    }
    pImpl->mQueueCapacity = maxQueueCapacity;
}

int ProxyOptions::getQueueCapacity() const noexcept
{
    return pImpl->mQueueCapacity;
}

