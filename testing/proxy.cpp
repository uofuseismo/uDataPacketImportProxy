#include <vector>
#include <cmath>
#include <string>
#include <chrono>
#include <queue>
#include <grpc/grpc.h>
#include <grpcpp/grpcpp.h>
#include <absl/log/initialize.h>
#include <google/protobuf/util/time_util.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include "uDataPacketImportAPI/v1/backend.grpc.pb.h"
#include "uDataPacketImportAPI/v1/frontend.grpc.pb.h"
#include "grpcOptions.hpp"
#include "frontendOptions.hpp"
#include "backendOptions.hpp"
#include "proxyOptions.hpp"
#include "proxy.hpp"
#include "packetUtilities.hpp"
import metrics;

#define FRONTEND_BIND_HOST "0.0.0.0"
#define FRONTEND_HOST "localhost"
#define FRONTEND_PORT 58151
//#define FRONTEND_PORT 50554

#define BACKEND_BIND_HOST "0.0.0.0"
#define BACKEND_HOST "localhost"
#define BACKEND_PORT 58152

std::vector<UDataPacketImportAPI::V1::Packet> referencePackets;

std::shared_ptr<spdlog::logger> proxyLogger{nullptr};

int64_t expectedPacketsReceived{0};
int64_t expectedPacketsSent{0};


bool comparePackets(const std::vector<UDataPacketImportAPI::V1::Packet> &lhs,
                    const std::vector<UDataPacketImportAPI::V1::Packet> &rhs)
{
    if (lhs.size() != rhs.size()){return true;}
    auto nPackets = static_cast<int> (lhs.size());
    for (int i = 0; i < nPackets; ++i)
    {
        if (lhs.at(i).number_of_samples() != rhs.at(i).number_of_samples())
        {
            return false;
        }
        if (std::abs(lhs[i].sampling_rate() - rhs[i].sampling_rate()) >
            1.e-14)
        {
            return false;
        }
        if (lhs[i].start_time() != rhs[i].start_time())
        {
            return false;
        }
        if (lhs[i].data_type() != rhs[i].data_type()){return false;}
        if (lhs[i].data() != rhs[i].data()){return false;}
    }
    return true;
}

void runProxy()
{
    
    UDataPacketImportProxy::GRPCOptions feGRPCOptions;
    feGRPCOptions.setHost(FRONTEND_BIND_HOST);
    feGRPCOptions.setPort(static_cast<uint16_t> (FRONTEND_PORT));
    UDataPacketImportProxy::FrontendOptions feOptions;
    feOptions.setGRPCOptions(feGRPCOptions);

    UDataPacketImportProxy::GRPCOptions beGRPCOptions;
    beGRPCOptions.setHost(BACKEND_BIND_HOST);
    beGRPCOptions.setPort(static_cast<uint16_t> (BACKEND_PORT));
    UDataPacketImportProxy::BackendOptions beOptions;
    beOptions.setGRPCOptions(beGRPCOptions);

    UDataPacketImportProxy::ProxyOptions proxyOptions;
    proxyOptions.setFrontendOptions(feOptions);
    proxyOptions.setBackendOptions(beOptions);    

    auto metrics
        = &UDataPacketImportProxy::Metrics::MetricsSingleton::getInstance();
    metrics->resetCounters();
    UDataPacketImportProxy::Proxy proxy{proxyOptions, proxyLogger};

    proxy.start();
    std::this_thread::sleep_for(std::chrono::seconds {3});
    proxy.stop();
    REQUIRE(metrics->getReceivedPacketsCount() == expectedPacketsReceived);
    REQUIRE(metrics->getSentPacketsCount() == expectedPacketsSent);
    //std::cout << metrics->getSentPacketsCount() << std::endl;
}

void asyncPacketPublisher(
    const std::vector<UDataPacketImportAPI::V1::Packet> &inputPackets)
{
    class Publisher final :
        public grpc::ClientWriteReactor<UDataPacketImportAPI::V1::Packet>
    {   
    public:
        Publisher(UDataPacketImportAPI::V1::Frontend::Stub *stub,
                  const std::queue<UDataPacketImportAPI::V1::Packet> &packets) :
            mPackets(packets)
        {
            stub->async()->Publish(&mContext, &mSummary, this);
            // Use a hold since some StartWrites are invoked indirectory from a 
            // delayed lambda in OnWriteDone rather than directly form the
            // reaction itself
            AddHold();
            nextWrite();
            StartCall();
        }
        void OnWriteDone(bool ok) override
        {
            if (ok)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds {10});
                //if (mPackets.size() == 2){std::this_thread::sleep_for(std::chrono::seconds {2});}
                //std::this_thread::sleep_for(std::chrono::seconds {1});
                nextWrite();
            }
        }
        void OnDone(const grpc::Status &status) override
        {
            std::unique_lock<std::mutex> lock(mMutex);
            mStatus = status;
            mDone = true;
            mConditionVariable.notify_one();
        }
        [[nodiscard]] grpc::Status await(UDataPacketImportAPI::V1::PublishResponse *response)
        {
            std::unique_lock<std::mutex> lock(mMutex);
            mConditionVariable.wait(lock, [this] {return mDone;});
            *response = mSummary;
            return std::move(mStatus);
        }
    private:
        void nextWrite()
        {
            if (!mPackets.empty())
            {
                mPacketToWrite = mPackets.front();
                mPackets.pop();
                StartWrite(&mPacketToWrite);
            }
            else
            {
                StartWritesDone();
                RemoveHold();
            }
        }
        std::mutex mMutex;
        std::condition_variable mConditionVariable;
        std::queue<UDataPacketImportAPI::V1::Packet> mPackets;
        UDataPacketImportAPI::V1::PublishResponse mSummary;
        grpc::ClientContext mContext;
        grpc::Status mStatus;
        UDataPacketImportAPI::V1::Packet mPacketToWrite;
        bool mDone{false};
    };

    UDataPacketImportAPI::V1::PublishResponse summary;

    std::queue<UDataPacketImportAPI::V1::Packet> packets;
    for (const auto &inputPacket : inputPackets)
    {
        packets.push(inputPacket);
    }

    auto address = std::string {FRONTEND_HOST}
                 + ":" + std::to_string(FRONTEND_PORT);
    auto channel
        = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
    //std::cout << address << std::endl;
    auto stub = UDataPacketImportAPI::V1::Frontend::NewStub(channel);
    Publisher publisher(stub.get(), packets);
    auto status = publisher.await(&summary);
    if (!status.ok())
    {
        throw std::runtime_error("Error publishing packets "
                               + status.error_message());
    }
}

void asyncSubscriber()
{
    class Subscriber final :
        public grpc::ClientReadReactor<UDataPacketImportAPI::V1::Packet>
    {
    public:
        Subscriber(UDataPacketImportAPI::V1::Backend::Stub *stub,
                   std::vector<UDataPacketImportAPI::V1::Packet> *receivedPackets) :
            mReceivedPackets(receivedPackets)
        {
            stub->async()->Subscribe(&mContext, &mRequest, this);
            StartRead(&mPacket);
            StartCall();
        }
        void OnReadDone(bool ok) override
        {
            if (ok)
            {   
#ifndef NDEBUG
                assert(mReceivedPackets);
#endif
                //std::cout << "yar" << std::endl;
                mReceivedPackets->push_back(mPacket);
                StartRead(&mPacket);
            }   
        }   
        void OnDone(const grpc::Status &status) override
        {
            std::unique_lock<std::mutex> lock(mMutex);
            mStatus = status;
            mDone = true;
            mConditionVariable.notify_one();
        }
//private:
        [[nodiscard]] grpc::Status await()
        {
            std::unique_lock<std::mutex> lock(mMutex);
            mConditionVariable.wait(lock, [this] {return mDone;});
            return std::move(mStatus);
        }
        std::mutex mMutex;
        std::condition_variable mConditionVariable;
        grpc::ClientContext mContext;
        UDataPacketImportAPI::V1::SubscriptionRequest mRequest;
        UDataPacketImportAPI::V1::Packet mPacket;
        grpc::Status mStatus;
        std::vector<UDataPacketImportAPI::V1::Packet> *mReceivedPackets{nullptr};
        bool mDone{false};
    };

    auto address = std::string {BACKEND_HOST}
                 + ":" + std::to_string(BACKEND_PORT);
    auto channel
        = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
    auto stub = UDataPacketImportAPI::V1::Backend::NewStub(channel);
    std::vector<UDataPacketImportAPI::V1::Packet> receivedPackets;
    Subscriber subscriber(stub.get(), &receivedPackets);
    auto status = subscriber.await();
    REQUIRE(status.ok());
    REQUIRE(::comparePackets(receivedPackets, referencePackets));
}

TEST_CASE("uDataPacketImportProxy::Proxy", "[streamSelector]")
{
    SECTION("Single Publisher/Single Subscriber")
    {
        proxyLogger = spdlog::stdout_color_mt("proxyConsoleLogger1");
        auto allPackets = ::generatePackets(5, "UU", "CWU", "HHZ", "01");
        auto hhnPackets = ::generatePackets(5, "UU", "CWU", "HHN", "01");
        auto hhePackets = ::generatePackets(5, "UU", "CWU", "HHE", "01");
        allPackets.insert(allPackets.end(), hhnPackets.begin(), hhnPackets.end());
        allPackets.insert(allPackets.end(), hhePackets.begin(), hhePackets.end());
        std::sort(allPackets.begin(), allPackets.end(),
                  [](const auto &lhs, const auto &rhs)
                  {   
                      return lhs.start_time() < rhs.start_time();
                  }); 
        referencePackets = allPackets;
        expectedPacketsReceived = allPackets.size();
        expectedPacketsSent = allPackets.size();

        auto proxyThread = std::thread(&runProxy);
        std::this_thread::sleep_for(std::chrono::milliseconds {50});

        auto subscriberThread1 = std::thread(&asyncSubscriber);
        std::this_thread::sleep_for(std::chrono::milliseconds {10});

        auto publisherThread = std::thread(&asyncPacketPublisher, allPackets);
        if (publisherThread.joinable()){publisherThread.join();}
        if (proxyThread.joinable()){proxyThread.join();}
        if (subscriberThread1.joinable()){subscriberThread1.join();}
        proxyLogger = nullptr;
    }

    SECTION("Single Publisher/Multiple Subscribers")
    {
        proxyLogger = spdlog::stdout_color_mt("proxyConsoleLogger2");
        auto allPackets = ::generatePackets(5, "UU", "CWU", "HHZ", "01");
        auto hhnPackets = ::generatePackets(5, "UU", "CWU", "HHN", "01");
        allPackets.insert(allPackets.end(), hhnPackets.begin(), hhnPackets.end());
        std::sort(allPackets.begin(), allPackets.end(),
                  [](const auto &lhs, const auto &rhs)
                  {   
                      return lhs.start_time() < rhs.start_time();
                  }); 
        referencePackets = allPackets;
        expectedPacketsReceived = allPackets.size();
        expectedPacketsSent = 2*allPackets.size();
    
        auto proxyThread = std::thread(&runProxy);
        std::this_thread::sleep_for(std::chrono::milliseconds {50});

        auto subscriberThread1 = std::thread(&asyncSubscriber);
        auto subscriberThread2 = std::thread(&asyncSubscriber);
        std::this_thread::sleep_for(std::chrono::milliseconds {10});

        auto publisherThread = std::thread(&asyncPacketPublisher, allPackets);
        if (publisherThread.joinable()){publisherThread.join();}
        if (proxyThread.joinable()){proxyThread.join();} 
        if (subscriberThread1.joinable()){subscriberThread1.join();}    
        if (subscriberThread2.joinable()){subscriberThread2.join();}
        proxyLogger = nullptr;
    }

}

