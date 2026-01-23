#ifndef UDATA_PACKET_IMPORT_PROXY_PROGRAM_OPTIONS_HPP
#define UDATA_PACKET_IMPORT_PROXY_PROGRAM_OPTIONS_HPP
#include <fstream>
#include <sstream>
#include <filesystem>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include "proxyOptions.hpp"
#include "frontendOptions.hpp"
#include "backendOptions.hpp"
#include "grpcOptions.hpp"
#define APPLICATION_NAME "uDataPacketImportProxy"

namespace
{
struct ProgramOptions
{
    std::string applicationName{APPLICATION_NAME};
    std::string prometheusURL{"localhost:9200"};
    UDataPacketImportProxy::ProxyOptions proxyOptions;
    int verbosity{3};
};

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
        options.setServerKey(::loadStringFromFile(serverKey));
        options.setServerCertificate(::loadStringFromFile(serverCertificate));
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
        options.setClientCertificate(::loadStringFromFile(clientCertificate));

    }


    return options;
}

UDataPacketImportProxy::FrontendOptions getFrontendOptions(
    const boost::property_tree::ptree &propertyTree)
{
    const std::string section{"Frontend"};
    UDataPacketImportProxy::FrontendOptions frontendOptions;
    constexpr bool isFrontend{true};
    auto grpcOptions = ::getGRPCOptions(propertyTree, section, isFrontend);
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
    auto grpcOptions = ::getGRPCOptions(propertyTree, section, isFrontend);
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
    auto frontendOptions = ::getFrontendOptions(propertyTree);
    auto backendOptions = ::getBackendOptions(propertyTree);
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

::ProgramOptions parseIniFile(const std::filesystem::path &iniFile)
{
    ::ProgramOptions options;
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


    options.proxyOptions = ::getProxyOptions(propertyTree); 
    return options;
}

}

#endif
