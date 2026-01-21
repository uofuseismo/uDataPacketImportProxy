#include <grpcpp/grpcpp.h>
#include <spdlog/spdlog.h>
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

}

class Backend::BackendImpl :
    public UDataPacketImportAPI::V1::Backend::CallbackService
{
public:
    void stop()
    {
        mKeepRunning.store(false);
        mNumberOfSubscribers.store(0);
    }

    void start()
    {
        mKeepRunning.store(true);
        mNumberOfSubscribers.store(0);
        auto grpcOptions = mOptions.getGRPCOptions();
        auto address = makeAddress(grpcOptions);
        grpc::ServerBuilder builder;
        if (grpcOptions.getServerKey() == std::nullopt ||
            grpcOptions.getServerCertificate() == std::nullopt)
        {
            spdlog::info("Initiating non-secured proxy backend");
            builder.AddListeningPort(address,
                                     grpc::InsecureServerCredentials());
            builder.RegisterService(this);
            mSecured = false;
        }
        else
        {
            spdlog::info("Initiating secured proxy backend");
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

        spdlog::info("Backend listening on " + address);
        mServer = builder.BuildAndStart();
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
    std::unique_ptr<grpc::Server> mServer{nullptr};
    std::atomic<int> mNumberOfSubscribers{0};
    std::atomic<bool> mKeepRunning{true};
    bool mSecured{false};
};

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

}
