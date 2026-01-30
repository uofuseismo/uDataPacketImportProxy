#include <vector>
#include <string>
#include <chrono>
#include <queue>
#include <grpc/grpc.h>
#include <grpcpp/grpcpp.h>
#include <absl/log/initialize.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <spdlog/spdlog.h>
#include "uDataPacketImportAPI/v1/backend.grpc.pb.h"
#include "uDataPacketImportAPI/v1/frontend.grpc.pb.h"
#include "grpcOptions.hpp"
#include "frontendOptions.hpp"
#include "backendOptions.hpp"
#include "proxyOptions.hpp"
#include "proxy.hpp"
#include "packetUtilities.hpp"

#define FRONTEND_BIND_HOST "0.0.0.0"
#define FRONTEND_HOST "localhost"
#define FRONTEND_PORT 58151
//#define FRONTEND_PORT 50554

#define BACKEND_BIND_HOST "0.0.0.0"
#define BACKEND_HOST "localhost"
#define BACKEND_PORT 58152


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

    std::shared_ptr<spdlog::logger> logger{nullptr};
    UDataPacketImportProxy::Proxy proxy{proxyOptions, logger};

    auto futures = proxy.start();
    std::this_thread::sleep_for(std::chrono::seconds {3});
    proxy.stop();
    for (auto &future : futures)
    {
        if (future.valid()){future.get();}
    }
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
            std::this_thread::sleep_for(std::chrono::milliseconds {10});
            //if (mPackets.size() == 2){std::this_thread::sleep_for(std::chrono::seconds {2});}
            //std::this_thread::sleep_for(std::chrono::seconds {1});
            nextWrite();
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
    if (status.ok())
    {
        std::cout << "got this many: " << receivedPackets.size() << std::endl;
    }
    else
    {
        std::cout << "oy" << std::endl;
    }
}

TEST_CASE("uDataPacketImportProxy::Proxy", "[streamSelector]")
{
    auto allPackets = ::generatePackets(5, "UU", "CWU", "HHZ", "01");
    auto hhnPackets = ::generatePackets(5, "UU", "CWU", "HHN", "01");
    allPackets.insert(allPackets.end(), hhnPackets.begin(), hhnPackets.end());
    std::sort(allPackets.begin(), allPackets.end(),
              [](const auto &lhs, const auto &rhs)
              {   
                  return lhs.start_time() < rhs.start_time();
              }); 
    
    auto proxyThread = std::thread(&runProxy);
    std::this_thread::sleep_for(std::chrono::milliseconds {50});

    auto subscriberThread = std::thread(&asyncSubscriber);
    std::this_thread::sleep_for(std::chrono::milliseconds {10});

    auto publisherThread = std::thread(&asyncPacketPublisher, allPackets);
    if (publisherThread.joinable()){publisherThread.join();}
    if (proxyThread.joinable()){proxyThread.join();} 
    if (subscriberThread.joinable()){subscriberThread.join();}    
}

