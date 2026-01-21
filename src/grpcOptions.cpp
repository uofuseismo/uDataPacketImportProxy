#include <string>
#include <algorithm>
#include <filesystem>
#include "grpcOptions.hpp"

namespace
{

/*
[[nodiscard]] std::string loadStringFromFile(const std::filesystem::path &path)
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
*/

}

using namespace UDataPacketImportProxy;

class GRPCOptions::GRPCOptionsImpl
{
public:
    std::string mHost{"localhost"};
    std::string mAccessToken;
    std::string mServerKey;
    std::string mServerCertificate;
    std::string mClientCertificate;
    uint16_t mPort{50000};
    bool mReflectionEnabled{false};
    bool mHaveServerCertificate{false}; 
    bool mHaveServerKey{false};
    bool mHaveClientCertificate{false};
    bool mHaveAccessToken{false};
};

/// Constructor
GRPCOptions::GRPCOptions() :
    pImpl(std::make_unique<GRPCOptionsImpl> ())
{
}

/// Copy constructor
GRPCOptions::GRPCOptions(const GRPCOptions &options)
{
    *this = options;
}

/// Move constructor
GRPCOptions::GRPCOptions(GRPCOptions &&options) noexcept
{
    *this = std::move(options);
}

/// Copy assignment
GRPCOptions& GRPCOptions::operator=(const GRPCOptions &options)
{
    if (&options == this){return *this;}
    pImpl = std::make_unique<GRPCOptionsImpl> (*options.pImpl);
    return *this;
}

/// Move assignment
GRPCOptions& GRPCOptions::operator=(GRPCOptions &&options) noexcept
{
    if (&options == this){return *this;}
    pImpl = std::move(options.pImpl);
    return *this;
}

/// Destructor
GRPCOptions::~GRPCOptions() = default; 

/// Host
void GRPCOptions::setHost(const std::string &hostIn)
{
    auto host = hostIn;
    host.erase(
       std::remove_if(host.begin(), host.end(), ::isspace),
       host.end());
    if (host.empty())
    {
        throw std::invalid_argument("Host is empty");
    }
    pImpl->mHost = host;
}

std::string GRPCOptions::getHost() const noexcept
{
    return pImpl->mHost;
}

std::string UDataPacketImportProxy::makeAddress(const GRPCOptions &options)
{
    return options.getHost() + ":" + std::to_string(options.getPort());
}

/// Port
void GRPCOptions::setPort(const uint16_t port)
{
    if (port < 1)
    {
        throw std::invalid_argument("port must be positive");
    }
    pImpl->mPort = port;
}

uint16_t GRPCOptions::getPort() const noexcept
{
    return pImpl->mPort;
}

/// Server certs
void GRPCOptions::setServerCertificate(const std::string &cert)
{
    if (cert.empty())
    {
        throw std::invalid_argument("Server certificate is empty");
    }
    pImpl->mHaveServerCertificate = true;
    pImpl->mServerCertificate = cert;
}

std::optional<std::string> GRPCOptions::getServerCertificate() const noexcept
{
    return pImpl->mHaveServerCertificate ? 
           std::make_optional<std::string> (pImpl->mServerCertificate) :
           std::nullopt;
}

void GRPCOptions::setServerKey(const std::string &key)
{   
    if (key.empty())
    {   
        throw std::invalid_argument("Server key is empty");
    }
    pImpl->mHaveServerKey = true;
    pImpl->mServerKey = key;
}

std::optional<std::string> GRPCOptions::getServerKey() const noexcept
{
    return pImpl->mHaveServerKey ? 
           std::make_optional<std::string> (pImpl->mServerKey) :
           std::nullopt;
}

/// Client cert
void GRPCOptions::setClientCertificate(const std::string &cert)
{
    if (cert.empty())
    {   
        throw std::invalid_argument("Client certificate is empty");
    }   
    pImpl->mHaveClientCertificate = true;
    pImpl->mClientCertificate = cert;
}

std::optional<std::string> GRPCOptions::getClientCertificate() const noexcept
{
    return pImpl->mHaveClientCertificate ? 
           std::make_optional<std::string> (pImpl->mClientCertificate) :
           std::nullopt;
}

/// Access token
void GRPCOptions::setAccessToken(const std::string &token)
{
    if (token.empty()){throw std::invalid_argument("Token is empty");}
    pImpl->mHaveAccessToken = true;
    pImpl->mAccessToken = token;
}

std::optional<std::string> GRPCOptions::getAccessToken() const noexcept
{
    return pImpl->mHaveAccessToken ?
           std::make_optional<std::string> (pImpl->mAccessToken) : std::nullopt;
}

/// Reflection
void GRPCOptions::enableReflection() noexcept
{
    pImpl->mReflectionEnabled = true;
}

void GRPCOptions::disableReflection() noexcept 
{
    pImpl->mReflectionEnabled = false;
}

bool GRPCOptions::reflectionEnabled() const noexcept
{ 
    return pImpl->mReflectionEnabled;
}
