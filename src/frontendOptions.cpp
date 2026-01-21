#include "frontendOptions.hpp"
#include "grpcOptions.hpp"

using namespace UDataPacketImportProxy;

class FrontendOptions::FrontendOptionsImpl
{
public:
    GRPCOptions mGRPCOptions;
    int mMaximumNumberOfPublishers{64};
    int mMaximumMessageSizeInBytes{8192};
    int mMaximumConsecutiveInvalidMessages{10};
};

/// Constructor
FrontendOptions::FrontendOptions() :
    pImpl(std::make_unique<FrontendOptionsImpl> ())
{
}

/// Copy constructor
FrontendOptions::FrontendOptions(const FrontendOptions &options)
{
    *this = options;
}

/// Move constructor
FrontendOptions::FrontendOptions(FrontendOptions &&options) noexcept
{
    *this = std::move(options);
}

/// Copy assignment
FrontendOptions& FrontendOptions::operator=(const FrontendOptions &options)
{
    if (&options == this){return *this;}
    pImpl = std::make_unique<FrontendOptionsImpl> (*options.pImpl);
    return *this;
}

/// Move assignment
FrontendOptions& FrontendOptions::operator=(FrontendOptions &&options) noexcept
{
    if (&options == this){return *this;}
    pImpl = std::move(options.pImpl);
    return *this;
}

/// Destructor
FrontendOptions::~FrontendOptions() = default;

/// GRPC options
void FrontendOptions::setGRPCOptions(const GRPCOptions &options)
{
    pImpl->mGRPCOptions = options;
}

GRPCOptions FrontendOptions::getGRPCOptions() const noexcept
{
    return pImpl->mGRPCOptions;
}

/// Max bad messages
void FrontendOptions::setMaximumNumberOfConsecutiveInvalidMessages(
    const int nMessages)
{
    if (nMessages < 0)
    {
        throw std::invalid_argument(
           "Maximum number of invalid messages must be non-negative"); 
    }
    pImpl->mMaximumConsecutiveInvalidMessages = nMessages;
}

int FrontendOptions::getMaximumNumberOfConsecutiveInvalidMessages()
    const noexcept
{
    return pImpl->mMaximumConsecutiveInvalidMessages;
}

/// Max message size in bytes
void FrontendOptions::setMaximumMessageSizeInBytes(const int maxSize)
{
    if (maxSize < 1)
    {
        throw std::invalid_argument("Maximum message size must be positive");
    }
    pImpl->mMaximumMessageSizeInBytes = maxSize;
}

int FrontendOptions::getMaximumMessageSizeInBytes() const noexcept
{
    return pImpl->mMaximumMessageSizeInBytes;
}

/// Max number of publishers
void FrontendOptions::setMaximumNumberOfPublishers(int maxPublishers)
{
    if (maxPublishers < 1)
    {   
        throw std::invalid_argument(
           "Maximum number of publishers must be positive");
    }   
    pImpl->mMaximumNumberOfPublishers = maxPublishers;
}

int FrontendOptions::getMaximumNumberOfPublishers() const noexcept
{
    return pImpl->mMaximumNumberOfPublishers;
}
