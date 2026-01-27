#include <thread>
#include <atomic>
#include <algorithm>
#ifndef NDEBUG
#include <cassert>
#endif
#include <boost/algorithm/string/trim.hpp>
#include <grpcpp/grpcpp.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include "uDataPacketImportAPI/v1/packet.pb.h"
#include "uDataPacketImportAPI/v1/frontend.grpc.pb.h"
#include "frontend.hpp"
#include "frontendOptions.hpp"
#include "grpcOptions.hpp"

using namespace UDataPacketImportProxy;

namespace
{

bool validatePublisher(const grpc::CallbackServerContext *context,
                       const std::string &accessToken)
{
    if (accessToken.empty()){return true;}
    for (const auto &item : context->client_metadata())
    {
        if (item.first == "x-custom-auth-token")
        {
            if (item.second == accessToken)
            {   
                //spdlog::info("Validated publisher " + peer + "'s token");
                return true;
            }   
        }
    }
    return false;
}

class AsynchronousReaderFrontend :
    public grpc::ServerReadReactor<UDataPacketImportAPI::V1::Packet> 
{
public:
    AsynchronousReaderFrontend(
        const FrontendOptions &options, 
        grpc::CallbackServerContext *context,
        const std::function<void (UDataPacketImportAPI::V1::Packet &&)> &callback,
        UDataPacketImportAPI::V1::PublishResponse *publishResponse,
        std::shared_ptr<spdlog::logger> &logger,
        const bool isSecured,
        std::atomic<int> *numberOfPublishers,
        std::atomic<bool> *keepRunning
    ) :
        mContext(context),
        mPublishResponse(publishResponse),
        mCallback(callback),
        mLogger(logger),
        mNumberOfPublishers(numberOfPublishers),
        mKeepRunning(keepRunning)
    {
        mMaximumNumberOfPublishers = options.getMaximumNumberOfPublishers();
        mMaximumConsecutiveInvalidMessages
            = options.getMaximumNumberOfConsecutiveInvalidMessages();
        mConsecutiveInvalidMessagesCounter = 0;
        if (mPublishResponse)
        {
            mPublishResponse->set_total_packets(0);
            mPublishResponse->set_packets_rejected(0);
        }   
        mPeer = mContext->peer();
        if (isSecured && options.getGRPCOptions().getAccessToken())
        {
            auto accessToken = *options.getGRPCOptions().getAccessToken();
            if (!::validatePublisher(mContext, accessToken))
            {
                SPDLOG_LOGGER_INFO(mLogger, "Frontend rejected {}", mPeer);
                grpc::Status status{grpc::StatusCode::UNAUTHENTICATED,
R"""(
Publisher must provide access token in x-custom-auth-token header field.
)"""};
                Finish(status);
            }
            else
            {
                SPDLOG_LOGGER_INFO(mLogger, "Frontend validated {}", mPeer);
            }
        }
        else
        {
            SPDLOG_LOGGER_INFO(mLogger, "{} connected to frontend", mPeer);
        }
        if (mNumberOfPublishers->load() >= mMaximumNumberOfPublishers)
        {
            SPDLOG_LOGGER_WARN(mLogger,
                "Frontend rejecting {} because max number of publishers hit",
                 mPeer);
            grpc::Status status{grpc::StatusCode::RESOURCE_EXHAUSTED,
                                "Max publishers hit - try again later"};
            Finish(status);
        }
        // Start
        mNumberOfPublishers->fetch_add(1);
        if (mKeepRunning->load())
        {
            StartRead(&mPacket);
        }
        else
        {
            SPDLOG_LOGGER_WARN(mLogger, "Immediately closing RPC publish");
            Finish(grpc::Status::OK);
        }
    }

    void OnReadDone(bool ok) override 
    {   
        if (ok) 
        {
            mTotalPackets++;
            auto packet = mPacket;
            if (packet.number_of_samples() > 0 &&
                packet.data_type() !=
                    UDataPacketImportAPI::V1::DATA_TYPE_UNKNOWN &&
                packet.sampling_rate() > 0)
            {
                // Stream identifier
                auto streamIdentifier = packet.stream_identifier();
                auto network = streamIdentifier.network();
                boost::algorithm::trim(network);
                std::transform(network.begin(), network.end(),
                               network.begin(), ::toupper);

                auto station = streamIdentifier.station();
                boost::algorithm::trim(station);
                std::transform(station.begin(), station.end(),
                               station.begin(), ::toupper);

                auto channel = streamIdentifier.channel();
                boost::algorithm::trim(channel);
                std::transform(channel.begin(), channel.end(),
                               channel.begin(), ::toupper);

                auto locationCode = streamIdentifier.location_code();
                boost::algorithm::trim(locationCode);
                std::transform(locationCode.begin(), locationCode.end(),
                               locationCode.begin(), ::toupper);
                if (locationCode.empty()){locationCode = "--";}
                if (!network.empty() && !station.empty() && !channel.empty())
                {
                    streamIdentifier.set_network(std::move(network));
                    streamIdentifier.set_station(std::move(station));
                    streamIdentifier.set_channel(std::move(channel));
#ifndef NDEBUG
                    assert(!locationCode.empty());
#endif
                    streamIdentifier.set_location_code(std::move(locationCode));
                    *packet.mutable_stream_identifier()
                        = std::move(streamIdentifier);
                    // Send it
                    try
                    {
                        mCallback(std::move(packet));
                        mConsecutiveInvalidMessagesCounter = 0;
                    }
                    catch (const std::exception &e) 
                    {
                        SPDLOG_LOGGER_WARN(mLogger, 
                                   "{} failed to submit packet because {}",
                                   mPeer, std::string {e.what()});
                        mPacketsRejected++;
                    }
                }
                else
                {
                    mPacketsRejected++;
                    mConsecutiveInvalidMessagesCounter++;
                }
            }
            else
            {
                // Skip packet and propagate
                mPacketsRejected++;
                mConsecutiveInvalidMessagesCounter++;
            }
            // Are we just constantly erroring out?
            if (mConsecutiveInvalidMessagesCounter >
                mMaximumConsecutiveInvalidMessages)
            {
                SPDLOG_LOGGER_WARN(mLogger,
                    "Frontend disconnecting {} because it sent too many consecutive invalid messages",
                    mPeer);
                grpc::Status status{grpc::StatusCode::INVALID_ARGUMENT,
                        "Too many conseuctive messages were invalid - double check API"};
                Finish(status);
            }
            if (mKeepRunning->load()){StartRead(&mPacket);}
        }
        else
        {
            if (mPublishResponse)
            {
                mPublishResponse->set_total_packets(mTotalPackets);
                mPublishResponse->set_packets_rejected(mPacketsRejected);
            }
            Finish(grpc::Status::OK);
        }
    } 

    void OnDone() override 
    {   
        SPDLOG_LOGGER_INFO(mLogger,
                           "Async packet proxy frontend RPC completed for {}",
                           mPeer);
        mNumberOfPublishers->fetch_sub(1);
        delete this;
    }   

    void OnCancel() override 
    {   
        SPDLOG_LOGGER_INFO(mLogger,
                           "Async packet proxy frontend RPC canceled by {}",
                           mPeer);
    }   
//private:
    grpc::CallbackServerContext *mContext{nullptr};
    std::function<void (UDataPacketImportAPI::V1::Packet &&)> mCallback;
    UDataPacketImportAPI::V1::PublishResponse *mPublishResponse{nullptr};
    std::shared_ptr<spdlog::logger> mLogger{nullptr};
    std::string mPeer;
    UDataPacketImportAPI::V1::Packet mPacket;
    int mConsecutiveInvalidMessagesCounter{0};
    int mMaximumNumberOfPublishers{32};
    int mMaximumConsecutiveInvalidMessages{10}; 
    uint64_t mTotalPackets{0};
    uint64_t mPacketsRejected{0};
    std::chrono::seconds mDeadline{std::chrono::minutes {15}};
    std::atomic<int> *mNumberOfPublishers{nullptr};
    std::atomic<bool> *mKeepRunning{nullptr};
};

}

class Frontend::FrontendImpl final :
    public UDataPacketImportAPI::V1::Frontend::CallbackService
{
public:
    FrontendImpl(
        const FrontendOptions &options,
        const std::function<void (UDataPacketImportAPI::V1::Packet &&)> &callback,
        std::shared_ptr<spdlog::logger> &logger) :
        mOptions(options),
        mAddPacketCallback(callback),
        mLogger(logger)
    {
        if (mLogger == nullptr)
        {   
            mLogger = spdlog::stdout_color_mt("ProxyFrontendConsole");
        }   
    }

    void stop()
    {
        mKeepRunning.store(false); 
        mNumberOfPublishers.store(0);
    }

    void start()
    {
        mKeepRunning.store(true);
        mNumberOfPublishers.store(0);
        auto grpcOptions = mOptions.getGRPCOptions();
        auto address = makeAddress(grpcOptions);
        grpc::ServerBuilder builder;
        //constexpr std::chrono::milliseconds maxIdle{std::chrono::seconds {1}};
        //auto idleOption = grpc::MakeChannelArgumentOption( GRPC_ARG_MAX_CONNECTION_IDLE_MS, maxIdle.count() );
        //builder.SetOption(std::move(idleOption));
        if (mOptions.getMaximumMessageSizeInBytes() > 0)
        {
            builder.SetMaxReceiveMessageSize(
                mOptions.getMaximumMessageSizeInBytes());
        }
        if (grpcOptions.getServerKey() == std::nullopt ||
            grpcOptions.getServerCertificate() == std::nullopt)
        { 
            SPDLOG_LOGGER_INFO(mLogger,
                               "Initiating non-secured proxy frontend");
            builder.AddListeningPort(address,
                                     grpc::InsecureServerCredentials());
            builder.RegisterService(this);
            mSecured = false;
        }
        else
        {   
            SPDLOG_LOGGER_INFO(mLogger, "Initiating secured proxy frontend");
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

        SPDLOG_LOGGER_INFO(mLogger, "Frontend listening at {}", address);
        mServer = builder.BuildAndStart();
    }

    /// The RPC
    grpc::ServerReadReactor<UDataPacketImportAPI::V1::Packet>*
        Publish(grpc::CallbackServerContext* context,
                UDataPacketImportAPI::V1::PublishResponse *publishResponse) override
    {
        return new ::AsynchronousReaderFrontend(
            mOptions,
            context,
            mAddPacketCallback,
            publishResponse,
            mLogger,
            mSecured,
            &mNumberOfPublishers,
            &mKeepRunning);
    }


    ~FrontendImpl() override
    {
        stop();
        std::this_thread::sleep_for(std::chrono::milliseconds {15});
        if (mServer)
        {   
            mServer->Shutdown();
        }   
    }
//private:
    FrontendOptions mOptions;
    std::function<void (UDataPacketImportAPI::V1::Packet &&)> mAddPacketCallback;
    std::shared_ptr<spdlog::logger> mLogger{nullptr};
    bool mSecured{false};
    std::unique_ptr<grpc::Server> mServer{nullptr};
    std::atomic<int> mNumberOfPublishers{0};
    std::atomic<bool> mKeepRunning{true};
};

Frontend::Frontend(
    const FrontendOptions &options,
    const std::function<void (UDataPacketImportAPI::V1::Packet &&)> &callback,
    std::shared_ptr<spdlog::logger> &logger) :
    pImpl(std::make_unique<FrontendImpl> (options, callback, logger))
{
}

/// Starts the frontend
void Frontend::start()
{   
    pImpl->start();
/*

    if (grpcOptions.getServerKey() == std::nullopt ||
        grpcOptions.getServerCertificate() == std::nullopt)
    { 
        spdlog::info("Initiating non-secured proxy frontend");
        builder.AddListeningPort(address,
                                 grpc::InsecureServerCredentials());
        builder.RegisterService(this);
    }
    if (grpcOptions.serverKey.empty() ||
        grpcOptions.serverCertificate.empty())
    {   
        spdlog::info("Initiating non-secured proxy frontend");
        builder.AddListeningPort(address,
                                 grpc::InsecureServerCredentials());
        builder.RegisterService(this);
    }   
    else
    {   
        spdlog::info("Initiating secured proxy frontend");
        grpc::SslServerCredentialsOptions::PemKeyCertPair keyCertPair
        {
            grpcOptions.serverKey,        // Private key
            grpcOptions.serverCertificate // Public key (cert chain)
        };
        grpc::SslServerCredentialsOptions sslOptions; 
        sslOptions.pem_key_cert_pairs.emplace_back(keyCertPair);
        builder.AddListeningPort(address,
                                 grpc::SslServerCredentials(sslOptions));
        builder.RegisterService(this);
    }   
    spdlog::info("Frontend listening on " + address);
    pImpl->mServer = builder.BuildAndStart();
*/

}

void Frontend::stop()
{
    pImpl->stop();
}

Frontend::~Frontend() = default;

