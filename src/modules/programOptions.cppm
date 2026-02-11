module;

#include <iostream>
#include <string>
#include <chrono>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <boost/program_options.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include "proxyOptions.hpp"
#include "frontendOptions.hpp"
#include "backendOptions.hpp"
#include "grpcOptions.hpp"

export module programOptions;

namespace UDataPacketImportProxy::Options
{

#define APPLICATION_NAME "uDataPacketImportProxy"

export struct OTelHTTPMetricsOptions
{
    std::string url{"localhost:4318"};
    std::chrono::milliseconds exportInterval{5000};
    std::chrono::milliseconds exportTimeOut{500};
    std::string suffix{"/v1/metrics"};
};

export struct OTelHTTPLogOptions
{
    std::string url{"localhost:4318"};
    std::filesystem::path certificatePath;
    std::string suffix{"/v1/logs"};
};

export struct ProgramOptions
{
    std::string applicationName{APPLICATION_NAME};
    OTelHTTPMetricsOptions otelHTTPMetricsOptions;
    OTelHTTPLogOptions otelHTTPLogOptions;
    UDataPacketImportProxy::ProxyOptions proxyOptions;
    std::chrono::seconds printSummaryInterval{3600};
    int verbosity{3};
    bool exportLogs{false};
    bool exportMetrics{false};
};

export
std::pair<std::string, bool> parseCommandLineOptions(int argc, char *argv[])
{
    std::string iniFile;
    boost::program_options::options_description desc(R"""(
The uDataPacketImportProxy is a high-speed fixed endpoint to which publishers
send acquired data packets to the proxy frontend.  Broadcast services can then
then subscribe to the backend and forward data packets in a way that better
enables downstream applications.

Example usage is:

    uDataPacketImportProxy --ini=proxy.ini

Allowed options)""");
    desc.add_options()
        ("help", "Produces this help message")
        ("ini",  boost::program_options::value<std::string> (), 
                 "The initialization file for this executable");
    boost::program_options::variables_map vm; 
    boost::program_options::store(
        boost::program_options::parse_command_line(argc, argv, desc), vm);
    boost::program_options::notify(vm);
    if (vm.count("help"))
    {   
        std::cout << desc << std::endl;
        return {iniFile, true};
    }   
    if (vm.count("ini"))
    {   
        iniFile = vm["ini"].as<std::string>();
        if (!std::filesystem::exists(iniFile))
        {
            throw std::runtime_error("Initialization file: " + iniFile
                                   + " does not exist");
        }
    }   
    else
    {   
        throw std::runtime_error("Initialization file not specified");
    }   
    return {iniFile, false};
}

[[nodiscard]] std::string
loadStringFromFile(const std::filesystem::path &path)
{
    std::string result;
    if (!std::filesystem::exists(path)){return result;}
    std::ifstream file(path);
    if (!file.is_open())
    {
        throw std::runtime_error("Failed to open " + path.string());
    }
    std::stringstream sstr;
    sstr << file.rdbuf();
    file.close();
    result = sstr.str();
    return result;
}


[[nodiscard]] UDataPacketImportProxy::GRPCOptions getGRPCOptions(
    const boost::property_tree::ptree &propertyTree,
    const std::string &section,
    const bool isFrontEnd)
{
    UDataPacketImportProxy::GRPCOptions options;

    auto host
        = propertyTree.get<std::string> (section + ".host",
                                         options.getHost());
    if (host.empty())
    {
        throw std::runtime_error(section + ".host is empty");
    }
    options.setHost(host);

    uint16_t port{50000};
    if (!isFrontEnd){port = 50001;}
    options.setPort(port);

    port = propertyTree.get<uint16_t> (section + ".port", options.getPort());
    options.setPort(port); 

    auto serverKey
        = propertyTree.get<std::string> (section + ".serverKey", "");
    auto serverCertificate
        = propertyTree.get<std::string> (section + ".serverCertificate", "");
    if (!serverKey.empty() && !serverCertificate.empty())
    {
        if (!std::filesystem::exists(serverKey))
        {
            throw std::invalid_argument("gRPC server key file "
                                      + serverKey + " does not exist");
        }
        if (!std::filesystem::exists(serverCertificate))
        {
            throw std::invalid_argument("gRPC server certificate file "
                                      + serverCertificate
                                      + " does not exist");
        }
        options.setServerKey(loadStringFromFile(serverKey));
        options.setServerCertificate(loadStringFromFile(serverCertificate));
    }
    
    auto accessToken
        = propertyTree.get_optional<std::string> (section + ".accessToken");
    if (accessToken)
    {
        if (options.getServerKey() == std::nullopt ||
            options.getServerCertificate() == std::nullopt)
        {
            throw std::invalid_argument(
                "Must set server certificate and key to use access token");
        }
        options.setAccessToken(*accessToken);
    }
     
    auto clientCertificate
        = propertyTree.get<std::string> (section + ".clientCertificate", "");
    if (!clientCertificate.empty())
    {
        if (!std::filesystem::exists(clientCertificate))
        {
            throw std::invalid_argument("gRPC client certificate file "
                                      + clientCertificate
                                      + " does not exist");
        }
        options.setClientCertificate(loadStringFromFile(clientCertificate));

    }


    return options;
}

UDataPacketImportProxy::FrontendOptions getFrontendOptions(
    const boost::property_tree::ptree &propertyTree)
{
    const std::string section{"Frontend"};
    UDataPacketImportProxy::FrontendOptions frontendOptions;
    constexpr bool isFrontend{true};
    auto grpcOptions = getGRPCOptions(propertyTree, section, isFrontend);
    frontendOptions.setGRPCOptions(grpcOptions);

    auto maxMessageSize = frontendOptions.getMaximumMessageSizeInBytes();
    maxMessageSize 
        = propertyTree.get<int> (section + ".maximumMessageSizeInBytes",
                                 maxMessageSize);
    frontendOptions.setMaximumMessageSizeInBytes(maxMessageSize);

    auto maxPublishers = frontendOptions.getMaximumNumberOfPublishers();
    maxPublishers
        = propertyTree.get<int> (section + ".maximumNumberOfPublishers",
                                 maxPublishers);
    frontendOptions.setMaximumNumberOfPublishers(maxPublishers);

    auto maxBadMessages
        = frontendOptions.getMaximumNumberOfConsecutiveInvalidMessages();
    maxBadMessages
        = propertyTree.get<int> (
             section + ".maximumNumberOfConsecutiveInvalidMessages",
             maxBadMessages); 
    frontendOptions.setMaximumNumberOfConsecutiveInvalidMessages(
        maxBadMessages);
    return frontendOptions;
} 

UDataPacketImportProxy::BackendOptions getBackendOptions(
    const boost::property_tree::ptree &propertyTree)
{
    const std::string section{"Backend"};
    UDataPacketImportProxy::BackendOptions backendOptions;
    constexpr bool isFrontend{false};
    auto grpcOptions = getGRPCOptions(propertyTree, section, isFrontend);
    backendOptions.setGRPCOptions(grpcOptions);

    auto maxSubscribers = backendOptions.getMaximumNumberOfSubscribers();
    maxSubscribers
        = propertyTree.get<int> (section + ".maximumNumberOfSubscribers",
                                 maxSubscribers);
    backendOptions.setMaximumNumberOfSubscribers(maxSubscribers);

    auto queueCapacity = backendOptions.getQueueCapacity();
    queueCapacity
        = propertyTree.get<int> (section + ".queueCapacity",
                                 queueCapacity);
    backendOptions.setQueueCapacity(queueCapacity);


    return backendOptions;
} 

UDataPacketImportProxy::ProxyOptions getProxyOptions(
    const boost::property_tree::ptree &propertyTree)
{
    UDataPacketImportProxy::ProxyOptions proxyOptions;

    auto queueCapacity = proxyOptions.getQueueCapacity();
    queueCapacity
        = propertyTree.get<int> ("Proxy.queueCapacity",
                                 queueCapacity);
    proxyOptions.setQueueCapacity(queueCapacity);

    auto frontendOptions = getFrontendOptions(propertyTree);
    auto backendOptions = getBackendOptions(propertyTree);
    if (frontendOptions.getGRPCOptions().getHost() == 
        backendOptions.getGRPCOptions().getHost())
    {
        if (frontendOptions.getGRPCOptions().getPort() ==
            backendOptions.getGRPCOptions().getPort())
        {
            throw std::invalid_argument(
               "Can't bind front and backend on same port");
        }
    }

    proxyOptions.setFrontendOptions(frontendOptions);
    proxyOptions.setBackendOptions(backendOptions);
    return proxyOptions;
}

std::string getOTelCollectorURL(boost::property_tree::ptree &propertyTree,
                                const std::string &section)
{
    std::string result;
    std::string otelCollectorHost 
        = propertyTree.get<std::string> (section + ".host", "");
    uint16_t otelCollectorPort
        = propertyTree.get<uint16_t> (section + ".port", 4218);
    if (!otelCollectorHost.empty())
    {
        result = otelCollectorHost + ":"
               + std::to_string(otelCollectorPort);
    }
    return result; 
}

export ProgramOptions 
    parseIniFile(const std::filesystem::path &iniFile)
{
    ProgramOptions options;
    if (!std::filesystem::exists(iniFile)){return options;}
    // Parse the initialization file
    boost::property_tree::ptree propertyTree;
    boost::property_tree::ini_parser::read_ini(iniFile, propertyTree);
    // Application name
    options.applicationName
        = propertyTree.get<std::string> ("General.applicationName",
                                         options.applicationName);
    if (options.applicationName.empty())
    {
        options.applicationName = APPLICATION_NAME;
    }
    options.verbosity
        = propertyTree.get<int> ("General.verbosity", options.verbosity);
    options.exportMetrics = false;
    options.exportLogs = false;

    // Metrics
    OTelHTTPMetricsOptions metricsOptions;
    metricsOptions.url
         = getOTelCollectorURL(propertyTree, "OTelHTTPMetricsOptions");
    metricsOptions.suffix
         = propertyTree.get<std::string> ("OTelHTTPMetricsOptions.suffix",
                                          "/v1/metrics");
    if (!metricsOptions.url.empty())
    {
        if (!metricsOptions.suffix.empty())
        {
            if (!metricsOptions.url.ends_with("/") &&
                !metricsOptions.suffix.starts_with("/"))
            {
                metricsOptions.suffix = "/" + metricsOptions.suffix;
            }
         }
    }
    if (!metricsOptions.url.empty())
    {
        options.exportMetrics = true;
        options.otelHTTPMetricsOptions = metricsOptions;
    }


    OTelHTTPLogOptions logOptions;
    logOptions.url
         = getOTelCollectorURL(propertyTree, "OTelHTTPLogOptions");
    logOptions.suffix 
         = propertyTree.get<std::string>
           ("OTelHTTPLogOptions.suffix", "/v1/logs");
    if (!logOptions.url.empty())
    {
        if (!logOptions.suffix.empty())
        {
            if (!logOptions.url.ends_with("/") &&
                !logOptions.suffix.starts_with("/"))
            {
                logOptions.suffix = "/" + logOptions.suffix;
            }
        }
    }
    if (!logOptions.url.empty())
    {
        options.exportLogs = true;
        options.otelHTTPLogOptions = logOptions;
    }
    
    options.proxyOptions = getProxyOptions(propertyTree); 
    return options;
}

}

