#include <atomic>
#include <thread>
#ifndef NDEBUG
#include <cassert>
#endif
#include <tbb/concurrent_queue.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include "proxy.hpp"
#include "proxyOptions.hpp"
#include "backend.hpp"
#include "backendOptions.hpp"
#include "frontend.hpp"
#include "frontendOptions.hpp"
#include "uDataPacketImportAPI/v1/packet.pb.h"

using namespace UDataPacketImportProxy;

class Proxy::ProxyImpl
{
public:
    explicit ProxyImpl(const ProxyOptions &options,
                       std::shared_ptr<spdlog::logger> &logger) :
        mOptions(options),
        mLogger(logger)
    {   
        if (mLogger == nullptr)
        {
            mLogger = spdlog::stdout_color_mt("ProxyConsole");
        }
        mImportExportQueueCapacity = mOptions.getQueueCapacity();
        mFrontend
            = std::make_unique<Frontend> (mOptions.getFrontendOptions(),
                                          mAddPacketCallback,
                                          mLogger);
        mBackend
            = std::make_unique<Backend>
              (mOptions.getBackendOptions(), mLogger);
        mImportExportQueue.set_capacity(mImportExportQueueCapacity);
    }   

    ~ProxyImpl()
    {
        stop();
    }

    void addPacketCallback(UDataPacketImportAPI::V1::Packet &&packet)
    {
        try
        {
            // Try to ensure there is enough space
            auto approximateSize
                = static_cast<int> (mImportExportQueue.size());
            while (approximateSize >= mImportExportQueueCapacity)
            {
                UDataPacketImportAPI::V1::Packet workSpace;
                if (!mImportExportQueue.try_pop(workSpace))
                {
                    SPDLOG_LOGGER_WARN(
                        mLogger,
                        "Failed to pop element from import queue");
                    break;
                }
                approximateSize = static_cast<int> (mImportExportQueue.size());
            }
            // Try to add the packet
            if (!mImportExportQueue.try_push(std::move(packet)))
            {
                SPDLOG_LOGGER_ERROR(
                    mLogger,
                    "Failed to add packet to import queue");
            }
        }
        catch (const std::exception &e) 
        {
            SPDLOG_LOGGER_ERROR(
                mLogger,
                "Failed to add packet to import queue because {}",
                std::string {e.what()});
        }
    }

    void propagatePacketToBackend()
    {
#ifndef NDEBUG
        assert(mBackend);
#endif
        constexpr std::chrono::milliseconds timeOut{15};
        while (mKeepRunning.load())
        {
            UDataPacketImportAPI::V1::Packet packet;
            if (mImportExportQueue.try_pop(packet))
            {
                try
                {
                    mBackend->enqueuePacket(std::move(packet));
                }
                catch (const std::exception &e) 
                {
                   SPDLOG_LOGGER_ERROR(
                      mLogger,
                "Failed to propagate packet to subscription manager because {}",
                      std::string {e.what()});
                }
            }
            else
            {
                std::this_thread::sleep_for(timeOut);
            }
        }
    }

    std::future<void> start()
    {   
#ifndef NDEBUG
        assert(mBackend);
        assert(mFrontend);
#endif
        stop();
        std::this_thread::sleep_for (std::chrono::milliseconds {10});

        mKeepRunning = true;
        auto result = std::async(&ProxyImpl::propagatePacketToBackend, this);
        // Technically starting the backend first will let the eager beavers
        // not miss a packet
        // N.B. start constructs the callback server so this can throw
        mBackend->start();
        // N.B. start constructs the callback server so this can throw
        mFrontend->start();
        return result;
    }

    void stop()
    {
        mKeepRunning = false;

        // Kill the importers first.  Closing the RPC will force the producers
        // to either fail or repoint to a new endpoint.  If the producers are
        // elegant then this will reduce the number of packets being lost.
        if (mFrontend)
        {
            SPDLOG_LOGGER_DEBUG(mLogger, "Proxy canceling RPCs on frontend");
            mFrontend->stop();
        }

        // Stop propagating packets
        if (mPropagatePacketThread.joinable()){mPropagatePacketThread.join();}

        // Now purge the subscribers.  By this point no new messages come in
        // but to help the subsribers out just a bit we'll pause just a moment
        // to give them a chance to finish pulling all the remaining data.
        std::this_thread::sleep_for (std::chrono::milliseconds {25});
        if (mBackend)
        {
            SPDLOG_LOGGER_DEBUG(mLogger, "Proxy canceling RPCs on backend");
            mBackend->stop();
        }
    }

    ProxyOptions mOptions;
    std::shared_ptr<spdlog::logger> mLogger{nullptr};
    std::function<void (UDataPacketImportAPI::V1::Packet &&)>
        mAddPacketCallback
    {   
        std::bind(&ProxyImpl::addPacketCallback,
                  this,
                  std::placeholders::_1)
    };  
    tbb::concurrent_bounded_queue<UDataPacketImportAPI::V1::Packet>
        mImportExportQueue;
    std::thread mPropagatePacketThread;
    std::unique_ptr<Backend> mBackend{nullptr};
    std::unique_ptr<Frontend> mFrontend{nullptr};
    size_t mImportExportQueueCapacity{8192};
    std::atomic<bool> mKeepRunning{true};
};

/// Constructor
Proxy::Proxy(const ProxyOptions &options,
             std::shared_ptr<spdlog::logger> &logger) :
    pImpl(std::make_unique<ProxyImpl> (options, logger))
{
}

/// Start the proxy
std::future<void> Proxy::start()
{
    return pImpl->start();
}

/// Stop the prxoy
void Proxy::stop()
{
    pImpl->stop();
}

/// Destructor
Proxy::~Proxy() = default;

