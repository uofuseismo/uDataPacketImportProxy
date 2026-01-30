#ifndef PACKET_UTILITIES_HPP
#define PACKET_UTILITIES_HPP
#include <string>
#include <vector>
#include <algorithm>
#include <random>
#include <chrono>
#include <bit>
#include <google/protobuf/util/time_util.h>
#include "uDataPacketImportAPI/v1/stream_identifier.pb.h"
#include "uDataPacketImportAPI/v1/packet.pb.h"

namespace
{

[[nodiscard]] std::chrono::microseconds getNowSimple() 
{
     auto now 
        = std::chrono::duration_cast<std::chrono::seconds>
          ((std::chrono::high_resolution_clock::now()).time_since_epoch());
     return now;
}

template<typename T>
std::string pack(const std::vector<T> &data, const bool swapBytes)
{
    constexpr auto dataTypeSize = sizeof(T); 
    std::string result;
    result.resize(dataTypeSize*data.size());
    // Pack it up
    union CharacterValueUnion
    {
        unsigned char cArray[dataTypeSize];
        T value;
    };
    CharacterValueUnion cvUnion;
    if (!swapBytes)
    {
        for (int i = 0; i < static_cast<int> (data.size()); ++i)
        {
            cvUnion.value = data[i]; 
            std::copy(cvUnion.cArray, cvUnion.cArray + dataTypeSize,
                      result.data() + dataTypeSize*i);
        }
    }
    else
    {
        for (int i = 0; i < static_cast<int> (data.size()); ++i)
        {
            cvUnion.value = data[i];     
            std::reverse_copy(cvUnion.cArray, cvUnion.cArray + dataTypeSize,
                              result.data() + dataTypeSize*i);
        }
    }
    return result;
}

template<typename T>
std::string pack(const std::vector<T> &data)
{
    bool swapBytes{std::endian::native == std::endian::little ? false : true};
    return ::pack(data, swapBytes);
}

std::vector<UDataPacketImportAPI::V1::Packet>
    generatePackets(int nPackets = 5,
                    const std::string &network = "UU",
                    const std::string &station = "CWU",
                    const std::string &channel = "HHZ",
                    const std::string &locationCode = "01")
{
    constexpr double samplingRate{100};
    std::vector<UDataPacketImportAPI::V1::Packet> result;
    auto startTimeMuS
        = ::getNowSimple()
        - std::chrono::seconds {nPackets*(300/100) + 1};
    std::mt19937 generator(23883823);
    std::uniform_int_distribution<int> distribution(200, 300);
    int sample{0};
    for (int i = 0; i < nPackets; ++i)
    {
        auto nSamples = distribution(generator);
        std::vector<int> data(nSamples);
        for (int k = 0; k < nSamples; ++k)
        {
            data[k] = sample;
            sample++;
        }
        auto packedData = ::pack(data);

        startTimeMuS
            = startTimeMuS
            + std::chrono::microseconds
              {
              static_cast<int64_t> (std::round(1000000*nSamples/samplingRate))
              };
        auto startTime
             = google::protobuf::util::TimeUtil::MicrosecondsToTimestamp(
                 startTimeMuS.count());

        UDataPacketImportAPI::V1::StreamIdentifier identifier;
        identifier.set_network(network);
        identifier.set_station(station);
        identifier.set_channel(channel);
        identifier.set_location_code(locationCode);

        UDataPacketImportAPI::V1::Packet packet;
        *packet.mutable_stream_identifier() = std::move(identifier);
        *packet.mutable_start_time() = startTime;
        packet.set_sampling_rate(samplingRate);
        packet.set_number_of_samples(data.size());
        packet.set_data_type(
            UDataPacketImportAPI::V1::DataType::DATA_TYPE_INTEGER_32);
        packet.set_data(packedData);
 
        result.push_back(std::move(packet));
    }
    return result;   
}


}

#endif
