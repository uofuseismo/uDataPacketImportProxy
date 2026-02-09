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
import metrics;

using namespace UDataPacketImportProxy;

class Proxy::ProxyImpl
{
public:
    explicit ProxyImpl(
        const ProxyOptions &options,
        std::shared_ptr<spdlog::logger> logger,
        UDataPacketImportProxy::Metrics::MetricsSingleton *metrics
    ) :
        mOptions(options),
        mLogger(logger),
        mMetrics(metrics)
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
            if (mImportExportQueue.try_push(std::move(packet)))
            {
                if (mMetrics)
                {
                    mMetrics->incrementReceivedPacketsCounter();
                }
            }
            else
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
        SPDLOG_LOGGER_DEBUG(mLogger, "Thread exiting propagate packet thread");
    }

    std::vector<std::future<void>> start()
    {   
        std::vector<std::future<void>> result;
#ifndef NDEBUG
        assert(mBackend);
        assert(mFrontend);
#endif
        stop();
        std::this_thread::sleep_for (std::chrono::milliseconds {10});

        mKeepRunning = true;
        // Get our propagator thread going before anything else
        result.push_back(
            std::async(&ProxyImpl::propagatePacketToBackend, this));
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
        // Kill the importers first.  Closing the RPC will force the producers
        // to either fail or repoint to a new endpoint.  If the producers are
        // elegant then this will reduce the number of packets being lost.
        if (mFrontend)
        {
            SPDLOG_LOGGER_DEBUG(mLogger, "Proxy canceling RPCs on frontend");
            mFrontend->stop();
        }
        std::this_thread::sleep_for (std::chrono::milliseconds {10});
 
        // Stop the packet propagator thread.  This gives a little more time for
        // the backend to finish its sends.
        mKeepRunning = false;

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
    UDataPacketImportProxy::Metrics::MetricsSingleton *mMetrics{nullptr};
    std::function<void (UDataPacketImportAPI::V1::Packet &&)>
        mAddPacketCallback
    {   
        std::bind(&ProxyImpl::addPacketCallback,
                  this,
                  std::placeholders::_1)
    };  
    tbb::concurrent_bounded_queue<UDataPacketImportAPI::V1::Packet>
        mImportExportQueue;
    std::unique_ptr<Backend> mBackend{nullptr};
    std::unique_ptr<Frontend> mFrontend{nullptr};
    int mImportExportQueueCapacity{8192};
    std::atomic<bool> mKeepRunning{true};
};

/// Constructor
Proxy::Proxy(const ProxyOptions &options,
             std::shared_ptr<spdlog::logger> logger,
             UDataPacketImportProxy::Metrics::MetricsSingleton *metrics) :
    pImpl(std::make_unique<ProxyImpl> (options, logger, metrics))
{
}

/// Start the proxy
std::vector<std::future<void>> Proxy::start()
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

/// Number of publishers
int Proxy::getNumberOfPublishers() const noexcept
{
    try 
    {
        if (pImpl->mFrontend)
        {
            return pImpl->mFrontend->getNumberOfPublishers();
         }
    }
    catch (const std::exception &e)
    {
        SPDLOG_LOGGER_ERROR(pImpl->mLogger,
                            "Failed to get number of publishers because {}",
                            std::string {e.what()});
    }
    return 0;
}

/// Number of subscribers
int Proxy::getNumberOfSubscribers() const noexcept
{
    try
    {
        if (pImpl->mBackend)
        {
            return pImpl->mBackend->getNumberOfSubscribers();
        }
    }
    catch (const std::exception &e) 
    {   
        SPDLOG_LOGGER_ERROR(pImpl->mLogger,
                            "Failed to get number of subscribers because {}",
                            std::string {e.what()});
    }   
    return 0;
}


