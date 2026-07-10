#include <string>
#include "uDataPacketImportProxy/version.hpp"

using namespace UDataPacketImportProxy;

int Version::getMajor() noexcept
{
    return uDataPacketImportProxy_MAJOR;
}

int Version::getMinor() noexcept
{
    return uDataPacketImportProxy_MINOR;
}

int Version::getPatch() noexcept
{
    return uDataPacketImportProxy_PATCH;
}

//NOLINTBEGIN(bugprone-easily-swappable-parameters)
bool Version::isAtLeast(const int major, const int minor,
                        const int patch) noexcept
//NOLINTEND(bugprone-easily-swappable-parameters)
{
    if (uDataPacketImportProxy_MAJOR < major){return false;}
    if (uDataPacketImportProxy_MAJOR > major){return true;}
    if (uDataPacketImportProxy_MINOR < minor){return false;}
    if (uDataPacketImportProxy_MINOR > minor){return true;}
    if (uDataPacketImportProxy_PATCH < patch){return false;}
    return true;
}

std::string Version::getVersion() noexcept
{
    std::string version{uDataPacketImportProxy_VERSION};
    return version;
}

std::string Version::getTag() noexcept
{
    std::string tag{uDataPacketImportProxy_GITTAG};
    return tag;
}

std::string Version::getVersionWithTag() noexcept
{
    auto tag = Version::getTag();
    if (tag.empty())
    {
        return Version::getVersion();
    }
    else
    {
        return Version::getVersion() + "-" + tag;
    }
}
