#ifndef OTEL_OPTIONS_HPP
#define OTEL_OPTIONS_HPP
#include <string>
#include <chrono>
#include <filesystem>

namespace UDataPacketImportProxy::OTelOptions
{

struct GRPCMetrics
{
    std::string url{"localhost:4317"};
    std::chrono::milliseconds exportInterval{std::chrono::seconds {15}};
    std::chrono::milliseconds exportTimeOut{500};
    std::filesystem::path certificatePath; // Path to the cert file
};

struct GRPCLog
{
    std::string url{"localhost:4317"};
    std::chrono::milliseconds exportTimeOut{500};
    std::filesystem::path certificatePath; // Path to the cert file
};


struct HTTPMetrics
{
    std::string url{"localhost:4318"};
    std::chrono::milliseconds exportInterval{std::chrono::seconds {15}};
    std::chrono::milliseconds exportTimeOut{500};
    std::string suffix{"/v1/metrics"};
};

struct HTTPLog
{
    std::string url{"localhost:4318"};
    std::filesystem::path certificatePath;
    std::string suffix{"/v1/logs"};
};

}

#endif
