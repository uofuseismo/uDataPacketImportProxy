#include <mutex>
#include <grpcpp/grpcpp.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <tbb/concurrent_queue.h>
#include <map>
#include "backend.hpp"
#include "backendOptions.hpp"
#include "grpcOptions.hpp"
#include "uDataPacketImportAPI/v1/packet.pb.h"
#include "uDataPacketImportAPI/v1/backend.grpc.pb.h"

using namespace UDataPacketImportProxy;

namespace
{

bool validateClient(const grpc::CallbackServerContext *context,
                    const std::string &accessToken,
                    const std::string &peer)
{
    if (accessToken.empty()){return true;}
    for (const auto &item : context->client_metadata())
    {
        if (item.first == "x-custom-auth-token")
        {
            if (item.second == accessToken)
            {
                spdlog::info("Validated " + peer + "'s token");
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
                 std::shared_ptr<spdlog::logger> &logger) :
        mLogger(logger)
    {
        if (queueCapacity < 1)
        {
            throw std::invalid_argument("Queue capacity must be positive");
        }
        mQueueCapacity = static_cast<size_t> (queueCapacity);
        mQueue.set_capacity(mQueueCapacity);
    }
    void enqueuePacket(const UDataPacketImportAPI::V1::Packet &packet)
    {
        enqueuePacket(std::move(packet));
    }
    void enqueuePacket(UDataPacketImportAPI::V1::Packet &&packet)
    {
        auto approximateSize = mQueue.size();
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
    size_t mQueueCapacity{32};
};

class SubscriptionManager
{
public:
    SubscriptionManager(const int queueCapacity,
                        std::shared_ptr<spdlog::logger> &logger) :
        mQueueCapacity(queueCapacity),
        mLogger(logger)
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

    mutable std::mutex mMutex;
    std::shared_ptr<spdlog::logger> mLogger{nullptr};
    std::map<uintptr_t, std::unique_ptr<::PacketStream>> mSubscribers;
    int mQueueCapacity{32};
    std::atomic<bool> mKeepRunning{true};
};

}

class Backend::BackendImpl :
    public UDataPacketImportAPI::V1::Backend::CallbackService
{
public:
    explicit BackendImpl(const BackendOptions &options,
                         std::shared_ptr<spdlog::logger> &logger) :
        mOptions(options),
        mLogger(logger)
    {
        if (mLogger == nullptr)
        {   
            mLogger = spdlog::stdout_color_mt("ProxyBackendConsole");
        }   
        mSubscriptionManager
            = std::make_unique<::SubscriptionManager>
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
    std::unique_ptr<::SubscriptionManager> mSubscriptionManager{nullptr};
    std::atomic<bool> mKeepRunning{true};
    bool mSecured{false};
};

/// Constructor
Backend::Backend(const BackendOptions &options,
                 std::shared_ptr<spdlog::logger> &logger) :
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
