#include <mutex>
#include <cmath>
#include <queue>
#include <map>
#include <grpcpp/grpcpp.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <tbb/concurrent_queue.h>
#include "backend.hpp"
#include "backendOptions.hpp"
#include "grpcOptions.hpp"
#include "uDataPacketImportAPI/v1/packet.pb.h"
#include "uDataPacketImportAPI/v1/backend.grpc.pb.h"
//#include "metrics.hpp"
import metrics;

using namespace UDataPacketImportProxy;

namespace
{

[[nodiscard]]
bool validateSubscriber(const grpc::CallbackServerContext *context,
                        const std::string &accessToken)
{
    if (accessToken.empty()){return true;}
    for (const auto &item : context->client_metadata())
    {
        if (item.first == "x-custom-auth-token")
        {
            if (item.second == accessToken)
            {
                return true;
            }
        }
    }   
    return false;
}

class PacketStream
{
public:
    PacketStream(const int queueCapacity,
                 std::shared_ptr<spdlog::logger> logger) :
        mLogger(logger)
    {
        if (queueCapacity < 1)
        {
            throw std::invalid_argument("Queue capacity must be positive");
        }
        mQueueCapacity = queueCapacity;
        mQueue.set_capacity(mQueueCapacity);
    }
    void enqueuePacket(const UDataPacketImportAPI::V1::Packet &packet)
    {
        auto copy = packet;
        enqueuePacket(std::move(copy));
    }
    void enqueuePacket(UDataPacketImportAPI::V1::Packet &&packet)
    {
        auto approximateSize = static_cast<int> (mQueue.size());
        if (approximateSize >= mQueueCapacity)
        {
            while (approximateSize >= mQueueCapacity)
            {
                UDataPacketImportAPI::V1::Packet workSpace;
                if (!mQueue.try_pop(workSpace))
                {
                    SPDLOG_LOGGER_WARN(mLogger,
                                       "Failed to pop element from stream queue");
                    break;
                }
                approximateSize = static_cast<int> (mQueue.size());
            }
        } 
        // Try to add the packet
        if (!mQueue.try_push(std::move(packet)))
        {
            SPDLOG_LOGGER_ERROR(mLogger,
                                 "Failed to add packet to stream queue");
        }
    }
    [[nodiscard]] std::optional<UDataPacketImportAPI::V1::Packet>
        dequeuePacket()
    {
        std::optional<UDataPacketImportAPI::V1::Packet> result{std::nullopt};
        UDataPacketImportAPI::V1::Packet packet;
        if (mQueue.try_pop(packet))
        {
            result
               = std::make_optional<UDataPacketImportAPI::V1::Packet>
                 (std::move(packet));
        }
        return result; 
    }
    std::shared_ptr<spdlog::logger> mLogger{nullptr};
    tbb::concurrent_bounded_queue<UDataPacketImportAPI::V1::Packet> mQueue;
    int mQueueCapacity{32};
};

class SubscriptionManager
{
public:
    SubscriptionManager(const int queueCapacity,
                        std::shared_ptr<spdlog::logger> logger) :
        mLogger(logger),
        mQueueCapacity(queueCapacity)
    {
        if (mLogger == nullptr)
        {
            mLogger = spdlog::stdout_color_mt("SubscriptionManagerConsole");
        }
    }

    ~SubscriptionManager()
    {
        unsubscribeAll();
    }

    // Number of subscribers
    [[nodiscard]] int getNumberOfSubscribers() const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return mSubscribers.size();
    }

    // Unsubscribes all
    void unsubscribeAll()
    {
        mKeepRunning.store(false);
        auto nSubscribers = getNumberOfSubscribers();
        {
        std::lock_guard<std::mutex> lock(mMutex);
        mSubscribers.clear();
        }
        if (nSubscribers > 0)
        {
            SPDLOG_LOGGER_INFO(mLogger,
                               "Subscription manager purged {} subscribers",
                               nSubscribers);
        }
    }

    // Subscribe
    void subscribe(grpc::CallbackServerContext *context)
    {
        if (!mKeepRunning.load()){return;}
        std::string errorMessage;
        bool alreadyExists{true};
        auto contextMemoryAddress = reinterpret_cast<uintptr_t> (context);
        {
        std::lock_guard<std::mutex> lock(mMutex);
        auto idx = mSubscribers.find(contextMemoryAddress);
        // Add it
        if (idx == mSubscribers.end())
        {
            alreadyExists = false;
            auto packetStream
                = std::make_unique<::PacketStream> (mQueueCapacity, mLogger);
            std::pair<uintptr_t, std::unique_ptr<::PacketStream>>
                newPacketStream{contextMemoryAddress, std::move(packetStream)};
            try
            {
                mSubscribers.insert(std::move(newPacketStream));
            }
            catch (const std::exception &e)
            {
                errorMessage = "Failed to subscribe " + context->peer()
                             + " because "
                             + std::string {e.what()}; 
            }
        }
        }
        if (!alreadyExists)
        {
            if (errorMessage.empty())
            {
                SPDLOG_LOGGER_INFO(mLogger, "Subscribed {}", context->peer());
            }
            else
            {
                throw std::runtime_error(errorMessage);
            } 
        }
    }

    // Unsubscribe
    void unsubscribe(grpc::CallbackServerContext *context)
    {
        if (!mKeepRunning.load()){return;}
        std::string errorMessage;
        bool exists{false};
        auto contextMemoryAddress = reinterpret_cast<uintptr_t> (context);
        {
        std::lock_guard<std::mutex> lock(mMutex);
        auto idx = mSubscribers.find(contextMemoryAddress);
        if (idx != mSubscribers.end())
        {
            exists = true;
            try
            {
                mSubscribers.erase(idx);
            }
            catch (const std::exception &e)
            {
                errorMessage = "Failed to unsubscribe " + context->peer()
                             + " because " + std::string {e.what()};
            }
        }
        }
        if (!exists)
        { 
            SPDLOG_LOGGER_WARN(mLogger, "{} was not subscribed",
                               context->peer());
        }
        else
        {
            if (!errorMessage.empty())
            {
                throw std::runtime_error(errorMessage);
            }
        }
    }

    /// Adds a packet
    void enqueuePacket(const UDataPacketImportAPI::V1::Packet &packet)
    {
        if (!mKeepRunning.load()){return;}
        std::lock_guard<std::mutex> lock(mMutex);
        for (auto &subscriber : mSubscribers)
        {
            try
            {
#ifndef NDEBUG
                assert(subscriber.second != nullptr);
#endif
                subscriber.second->enqueuePacket(packet);
            }
            catch (const std::exception &e)
            {
                SPDLOG_LOGGER_ERROR(mLogger,
                     "Subscription manager failed to enqueue packet because {}",
                     std::string {e.what()}); 
            } 
        }
    } 

    // Get next batch of packets
    [[nodiscard]] std::vector<UDataPacketImportAPI::V1::Packet> getNextPackets(
        grpc::CallbackServerContext *context,
        const int maxPackets)
    {
        std::vector<UDataPacketImportAPI::V1::Packet> result;
        result.reserve(8);
        if (!mKeepRunning.load()){return result;}
        bool exists{false};
        auto contextMemoryAddress = reinterpret_cast<uintptr_t> (context);
        {   
        std::lock_guard<std::mutex> lock(mMutex);
        auto idx = mSubscribers.find(contextMemoryAddress);
        if (idx != mSubscribers.end())
        {
            exists = true;
            for (int i = 0; i < maxPackets; ++i)
            {
                auto packet = idx->second->dequeuePacket();
                if (packet)
                {
                    result.push_back(std::move(*packet));
                }
                else
                {
                    break;
                }
            }
        }
        else
        {
            exists = false;
        }
        }
        if (!exists)
        {
            auto errorMessage = context->peer()
                              + " was not found in subscriber map"; 
            throw std::runtime_error(errorMessage);
        }
        return result;
    }

    mutable std::mutex mMutex;
    std::shared_ptr<spdlog::logger> mLogger{nullptr};
    std::map<uintptr_t, std::unique_ptr<::PacketStream>> mSubscribers;
    int mQueueCapacity{32};
    std::atomic<bool> mKeepRunning{true};
};

class AsynchronousWriter :
    public grpc::ServerWriteReactor<UDataPacketImportAPI::V1::Packet>
{
public:
    AsynchronousWriter(
        const BackendOptions &options,
        grpc::CallbackServerContext *context,
        const UDataPacketImportAPI::V1::SubscriptionRequest *, //request,
        std::shared_ptr<::SubscriptionManager> subscriptionManager,
        std::shared_ptr<spdlog::logger> logger,
        const bool isSecured,
        std::atomic<bool> *keepRunning) :
        mOptions(options),
        mContext(context),
        mSubscriptionManager(subscriptionManager),
        mLogger(logger),
        mKeepRunning(keepRunning)
    {
        auto maximumNumberOfSubscribers
            = mOptions.getMaximumNumberOfSubscribers();
        // Authenticate
        mPeer = context->peer();
        if (isSecured &&
            mOptions.getGRPCOptions().getAccessToken() != std::nullopt)
        {
            auto accessToken = *mOptions.getGRPCOptions().getAccessToken();
            if (!::validateSubscriber(mContext, accessToken))
            {
                SPDLOG_LOGGER_INFO(mLogger, "Backend rejected {}", mPeer);
                grpc::Status status{grpc::StatusCode::UNAUTHENTICATED,
R"""(
Subscriber must provide access token in x-custom-auth-token header field.
)"""};
                Finish(status);
            }
            else
            {
                SPDLOG_LOGGER_INFO(mLogger, "Backend validated {}", mPeer);
            }
        }
        else
        {
            SPDLOG_LOGGER_INFO(mLogger, "{} connected to backend", mPeer);
        }
        if (mSubscriptionManager->getNumberOfSubscribers() >=
            maximumNumberOfSubscribers)
        {
            SPDLOG_LOGGER_WARN(mLogger,
                "Backend rejecting {} because max number of subscribers hit",
                 mPeer);
            grpc::Status status{grpc::StatusCode::RESOURCE_EXHAUSTED,
                                "Max subscribers hit - try again later"};
            Finish(status);
        }
        // Subscribe
        try
        {
            SPDLOG_LOGGER_INFO(mLogger, "Subscribing {} to all streams", mPeer);
            mSubscriptionManager->subscribe(mContext);
            mSubscribed = true;
            auto nSubscribers = mSubscriptionManager->getNumberOfSubscribers();
            auto utilization
                = static_cast<double> (nSubscribers)
                 /std::max(1, maximumNumberOfSubscribers); 
            mMetrics.updateSubscriberUtilization(utilization);
            SPDLOG_LOGGER_INFO(mLogger,
                          "Backend is now managing {} subscribers (Resource {} pct utilized)",
                          nSubscribers, utilization*100.0);
        }
        catch (const std::exception &e)
        {
            SPDLOG_LOGGER_WARN(mLogger, "{} failed to subscribe because {}",
                               mPeer, std::string {e.what()});
            Finish(grpc::Status(grpc::StatusCode::INTERNAL,
                                "Failed to subscribe"));
        }
        // Start
        nextWrite();
    }

    ~AsynchronousWriter()
    {
        SPDLOG_LOGGER_INFO(mLogger, "In destructor");
    }


    void OnWriteDone(bool ok) override
    {
        if (!ok)
        {
            if (mContext)
            {
                if (mContext->IsCancelled())
                {
                    return Finish(grpc::Status::CANCELLED);
                }
            }
            return Finish(grpc::Status(grpc::StatusCode::UNKNOWN,
                                       "Unexpected failure"));
        }
        // Packet is flushed; can now safely purge the element to write
        mWriteInProgress = false;
        mPacketsQueue.pop();
        mMetrics.incrementSentPacketsCounter();
        // Start next write
        nextWrite();
    }

    // This needs to perform quickly.  I should do blocking work but
    // this is my last ditch effort to evict the context from the 
    // subscription manager..
    void OnDone() override 
    { 
        if (mContext && mSubscribed)
        {
            mSubscriptionManager->unsubscribe(mContext);
            mSubscribed = false;
        }
        int nSubscribers = mSubscriptionManager->getNumberOfSubscribers();
        auto maximumNumberOfSubscribers
            = mOptions.getMaximumNumberOfSubscribers();
        auto utilization
            = static_cast<double> (std::max(0, nSubscribers))
             /std::max(1, maximumNumberOfSubscribers); 
        mMetrics.updateSubscriberUtilization(utilization);
        SPDLOG_LOGGER_INFO(mLogger,
  "Subscribe RPC completed for {}.  Backend is now managing {} subscribers.  (Resource {} pct utilized)",
                           mPeer, nSubscribers, utilization*100.0);
        delete this;
    }   

    void OnCancel() override 
    {   
        SPDLOG_LOGGER_INFO(mLogger, "Subscribe RPC cancelled for {}", mPeer);
        if (mContext && mSubscribed)
        {
            mSubscriptionManager->unsubscribe(mContext);
            mSubscribed = false;
        }    
    }   

private:
    void nextWrite()
    {
        // Keep running either until the server or client quits
        while (mKeepRunning->load())
        {
            // Cancel means we leave now
            if (mContext->IsCancelled()){break;}

            // Get any remaining packets on the queue on the wire
            if (!mPacketsQueue.empty() && !mWriteInProgress)
            {
                const auto &packet = mPacketsQueue.front();
                mWriteInProgress = true;
                StartWrite(&packet);
                return;
            }

            // I've cleared the queue and/or I have packets in flight.
            // Try to get more packets to write while I `wait.'
            if (mPacketsQueue.empty())
            {
                try
                {
                    auto packetsBuffer
                        = mSubscriptionManager->getNextPackets(
                             mContext,
                             mMaximumWriteQueueSize);
                    for (auto &packet : packetsBuffer)
                    {
                        if (mPacketsQueue.size() > mMaximumWriteQueueSize)
                        {
                            SPDLOG_LOGGER_WARN(mLogger,
                               "RPC writer queue exceeded - popping element");
                            mPacketsQueue.pop();
                        }
                        mPacketsQueue.push(std::move(packet));
                    }
                }
                catch (const std::exception &e)
                {
                    SPDLOG_LOGGER_WARN(mLogger,
                                       "Failed to get next packets because {}",
                                       std::string {e.what()});
                }
            }
            // No new packets were acquired and I'm not waiting for a write.
            // Give me stream manager a break.
            if (mPacketsQueue.empty() && !mWriteInProgress)
            {
                std::this_thread::sleep_for(mTimeOut);
            }
        } // Loop on server still running
        if (mContext)
        {
            // The context is still valid so try to remove it from the
            // subscriptoins.  This can be the case whether the server is
            // shutting down or the client bailed.
            mSubscriptionManager->unsubscribe(mContext);
            if (mContext->IsCancelled())
            {
                SPDLOG_LOGGER_INFO(mLogger,
                 "Terminating acquisition for {} because of client side cancel",
                    mPeer);
                Finish(grpc::Status::CANCELLED);
            }
            else
            {
                SPDLOG_LOGGER_INFO(mLogger,
                 "Terminating acquisition for {} because of server side cancel",
                    mPeer);
                Finish(grpc::Status::OK);
            }
        }
        else
        {
            SPDLOG_LOGGER_WARN(mLogger,
                               "The context for {} has disappeared", mPeer);
        }

    }
    BackendOptions mOptions;
    grpc::CallbackServerContext *mContext{nullptr};
    std::shared_ptr<::SubscriptionManager> mSubscriptionManager{nullptr};
    std::shared_ptr<spdlog::logger> mLogger{nullptr};
    UDataPacketImportProxy::Metrics::MetricsSingleton &mMetrics
    {
        UDataPacketImportProxy::Metrics::MetricsSingleton::getInstance()
    };
    std::atomic<bool> *mKeepRunning{nullptr};
    std::queue<UDataPacketImportAPI::V1::Packet> mPacketsQueue;
    std::string mPeer;
    std::chrono::milliseconds mTimeOut{20};
    size_t mMaximumWriteQueueSize{128};
    bool mWriteInProgress{false};
    bool mSubscribed{false};
};

}

class Backend::BackendImpl :
    public UDataPacketImportAPI::V1::Backend::CallbackService
{
public:
    explicit BackendImpl(const BackendOptions &options,
                         std::shared_ptr<spdlog::logger> logger) :
        mOptions(options),
        mLogger(logger)
    {
        if (mLogger == nullptr)
        {   
            mLogger = spdlog::stdout_color_mt("ProxyBackendConsole");
        }   
        mSubscriptionManager
            = std::make_shared<::SubscriptionManager>
              (mOptions.getQueueCapacity(), mLogger);
    }

    void stop()
    {
        mKeepRunning.store(false);
        mSubscriptionManager->unsubscribeAll();
    }

    void start()
    {
        mSubscriptionManager->mKeepRunning.store(true);
        mKeepRunning.store(true);
        auto grpcOptions = mOptions.getGRPCOptions();
        auto address = makeAddress(grpcOptions);
        grpc::ServerBuilder builder;
        if (grpcOptions.getServerKey() == std::nullopt ||
            grpcOptions.getServerCertificate() == std::nullopt)
        {
            SPDLOG_LOGGER_INFO(mLogger, "Initiating non-secured proxy backend");
            builder.AddListeningPort(address,
                                     grpc::InsecureServerCredentials());
            builder.RegisterService(this);
            mSecured = false;
        }
        else
        {
            SPDLOG_LOGGER_INFO(mLogger, "Initiating secured proxy backend");
            grpc::SslServerCredentialsOptions::PemKeyCertPair keyCertPair
            {
                *grpcOptions.getServerKey(),        // Private key
                *grpcOptions.getServerCertificate() // Public key (cert chain)
            };
            grpc::SslServerCredentialsOptions sslOptions; 
            sslOptions.pem_key_cert_pairs.emplace_back(keyCertPair);
            builder.AddListeningPort(address,
                                     grpc::SslServerCredentials(sslOptions));
            builder.RegisterService(this);
            mSecured = true;
        }

        SPDLOG_LOGGER_INFO(mLogger, "Backend listening at {}", address);
        mServer = builder.BuildAndStart();
    }

    [[nodiscard]] int getNumberOfSubscribers() const noexcept
    {
        return mSubscriptionManager->getNumberOfSubscribers();
    }

    grpc::ServerWriteReactor<UDataPacketImportAPI::V1::Packet> *
        Subscribe(grpc::CallbackServerContext* context,
                  const UDataPacketImportAPI::V1::SubscriptionRequest *request) override
    {
        return new ::AsynchronousWriter(mOptions,
                                        context,
                                        request,
                                        mSubscriptionManager,
                                        mLogger,
                                        mSecured,
                                        &mKeepRunning);
    }

    ~BackendImpl() override
    {   
        stop();
        std::this_thread::sleep_for(std::chrono::milliseconds {15});
        if (mServer)
        {   
            mServer->Shutdown();
        }   
    }   

    BackendOptions mOptions;
    std::shared_ptr<spdlog::logger> mLogger{nullptr};
    std::unique_ptr<grpc::Server> mServer{nullptr};
    std::shared_ptr<::SubscriptionManager> mSubscriptionManager{nullptr};
    std::atomic<bool> mKeepRunning{true};
    bool mSecured{false};
};

/// Constructor
Backend::Backend(const BackendOptions &options,
                 std::shared_ptr<spdlog::logger> logger) :
    pImpl(std::make_unique<BackendImpl> (options, logger))
{
}

/// Destructor
Backend::~Backend() = default;

/// Start
void Backend::start()
{
    pImpl->start();
}

/// Stop
void Backend::stop()
{
    pImpl->stop();
}

/// Enqueue packet
void Backend::enqueuePacket(UDataPacketImportAPI::V1::Packet &&packet)
{
    auto copy = packet;
    pImpl->mSubscriptionManager->enqueuePacket(std::move(copy));;
}

/// Number of subscribers
int Backend::getNumberOfSubscribers() const
{
    return std::max(0, pImpl->mSubscriptionManager->getNumberOfSubscribers());
}

bool Backend::isRunning() const noexcept
{
    return pImpl->mKeepRunning.load();
}
