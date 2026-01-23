#include "backendOptions.hpp"
#include "grpcOptions.hpp"

using namespace UDataPacketImportProxy;

class BackendOptions::BackendOptionsImpl
{
public:
    GRPCOptions mGRPCOptions;
    int mMaximumNumberOfSubscribers{32};
    int mQueueCapacity{32};
};

/// Constructor
BackendOptions::BackendOptions() :
    pImpl(std::make_unique<BackendOptionsImpl> ())
{
}

/// Copy constructor
BackendOptions::BackendOptions(const BackendOptions &options)
{
    *this = options;
}

/// Move constructor
BackendOptions::BackendOptions(BackendOptions &&options) noexcept
{
    *this = std::move(options);
}

/// Copy assignment
BackendOptions& BackendOptions::operator=(const BackendOptions &options)
{
    if (&options == this){return *this;}
    pImpl = std::make_unique<BackendOptionsImpl> (*options.pImpl);
    return *this;
}

/// Move assignment
BackendOptions& BackendOptions::operator=(BackendOptions &&options) noexcept
{
    if (&options == this){return *this;}
    pImpl = std::move(options.pImpl);
    return *this;
}

/// Destructor
BackendOptions::~BackendOptions() = default;

/// GRPC options
void BackendOptions::setGRPCOptions(const GRPCOptions &options)
{
    pImpl->mGRPCOptions = options;
}

GRPCOptions BackendOptions::getGRPCOptions() const noexcept
{
    return pImpl->mGRPCOptions;
}

/// Max number of subscribers
void BackendOptions::setMaximumNumberOfSubscribers(int maxSubscribers)
{
    if (maxSubscribers < 1)
    {   
        throw std::invalid_argument(
           "Maximum number of subscribers must be positive");
    }   
    pImpl->mMaximumNumberOfSubscribers = maxSubscribers;
}

int BackendOptions::getMaximumNumberOfSubscribers() const noexcept
{
    return pImpl->mMaximumNumberOfSubscribers;
}

/// Queue capacity
void BackendOptions::setQueueCapacity(const int capacity)
{
    if (capacity < 1)
    { 
        throw std::invalid_argument("Queue capacity must be positive");
    }
    pImpl->mQueueCapacity = capacity;
}

int BackendOptions::getQueueCapacity() const noexcept
{
    return pImpl->mQueueCapacity;
}
