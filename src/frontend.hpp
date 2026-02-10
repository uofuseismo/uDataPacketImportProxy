#ifndef UDATA_PACKET_IMPORT_PROXY_FRONTEND_HPP
#define UDATA_PACKET_IMPORT_PROXY_FRONTEND_HPP
#include <memory>
#include <functional>
#include <atomic>
//#include "frontendOptions.hpp"
//#include "uDataPacketImportAPI/v1/frontend.grpc.pb.h"
namespace UDataPacketImportAPI::V1
{
 class Packet;
}
namespace UDataPacketImportProxy
{
 class FrontendOptions;
}
namespace UDataPacketImportProxy
{
/// @class Frontend frontend.hpp
/// @brief Publishers send packets to the proxy's frontend.
/// @copyright Ben Baker (University of Utah) distributed under the
///            MIT NO AI license.
class Frontend final 
{
public:
    /// @brief Constructs the frontend with the given options.
    Frontend(const FrontendOptions &options,
             const std::function<void (UDataPacketImportAPI::V1::Packet &&)> &callback,
             std::shared_ptr<spdlog::logger> logger);
 
    /// @brief Starts the frontend.
    void start();

    /// @brief Terminates any running threads, cancels RPCs, and shuts
    ///        down the frontend.
    void stop();

//    grpc::ServerReadReactor<UDataPacketImportAPI::V1::Packet>*
//        Publish(grpc::CallbackServerContext* context,
//                UDataPacketImportAPI::V1::PublishResponse *publishResponse) override;

    /// @result The number of publishers.
    [[nodiscard]] int getNumberOfPublishers() const;
    /// @result True indicates that the frontend is running.
    [[nodiscard]] bool isRunning() const noexcept;

    ~Frontend();

    Frontend() = delete;
    Frontend(const Frontend &) = delete;
    Frontend(Frontend &&) noexcept = delete;
    Frontend& operator=(const Frontend &) = delete;
    Frontend& operator=(Frontend &&) noexcept = delete;
private:
    class FrontendImpl;
    std::unique_ptr<FrontendImpl> pImpl;
/*
    FrontendOptions mOptions;
    std::function<void (UDataPacketImportAPI::V1::Packet &&)> mAddPacketCallback;
    std::shared_ptr<spdlog::logger> mLogger{nullptr};
    bool mSecured{false};
    std::unique_ptr<grpc::Server> mServer{nullptr};
    std::atomic<int> mNumberOfPublishers{0};
    std::atomic<bool> mKeepRunning{true};
*/
};
}
#endif
