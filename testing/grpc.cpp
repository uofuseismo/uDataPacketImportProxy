#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <catch2/catch_test_macros.hpp>
#include "grpcOptions.hpp"

TEST_CASE("UPacketImportProxy", "[grpcOptions]")
{
    SECTION("Defaults")
    {
        const UDataPacketImportProxy::GRPCOptions options;
        REQUIRE(options.getHost() == "localhost");
        REQUIRE(options.getPort() == 50000);
        REQUIRE(options.getAccessToken() == std::nullopt);
        REQUIRE(options.getServerCertificate() == std::nullopt);
        REQUIRE(options.getServerKey() == std::nullopt);
        REQUIRE(options.getClientCertificate() == std::nullopt);
        REQUIRE(options.reflectionEnabled() == false);
    }

    SECTION("Options")
    {
        const std::string host{"some.host.org"};
        const std::string token{"super-secret-token"};
        const std::string serverCertificate{"some-wonky-hash"};
        const std::string serverKey{"some-private-wonky-hash"};
        const std::string clientCertificate{"some-other-hash"};
        //const std::string clientKey{"some-private-hash"};
        constexpr uint16_t port{12345};
        UDataPacketImportProxy::GRPCOptions options;

        options.setHost(host);
        options.setPort(port);
        options.setServerCertificate(serverCertificate);
        options.setServerKey(serverKey);
        options.setAccessToken(token);
        options.setClientCertificate(clientCertificate);
        options.enableReflection();

        REQUIRE(options.getHost() == host);
        REQUIRE(options.getPort() == port);
        REQUIRE(options.getServerCertificate() != std::nullopt);
        REQUIRE(options.getServerKey() != std::nullopt);
        REQUIRE(options.getAccessToken() != std::nullopt);
        REQUIRE(*options.getServerCertificate() == serverCertificate); //NOLINT
        REQUIRE(*options.getServerKey() == serverKey); //NOLINT
        REQUIRE(*options.getAccessToken() == token); //NOLINT
        REQUIRE(*options.getClientCertificate() == clientCertificate); //NOLINT
        REQUIRE(options.reflectionEnabled() == true);
    }
}
