#include <iostream>
#include <mutex>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <map>
#include <set>
#include <string>
#ifndef NDEBUG
#include <cassert>
#endif
#include <boost/circular_buffer.hpp>
#include <spdlog/spdlog.h>
#include <google/protobuf/util/time_util.h>
#include "duplicatePacketDetector.hpp"
#include "uDataPacketImportAPI/v1/packet.pb.h"

using namespace UDataPacketImportProxy;

namespace
{

[[nodiscard]] std::string toName(
    const UDataPacketImportAPI::V1::StreamIdentifier &streamIdentifier)
{
    auto result = streamIdentifier.network() + "."
                + streamIdentifier.station() + "."
                + streamIdentifier.channel() + ".";
    if (!streamIdentifier.location_code().empty())
    {
         result = result + "." + streamIdentifier.location_code();
    }
    return result;
}

[[nodiscard]]
std::string toName(const UDataPacketImportAPI::V1::Packet &packet)
{
    return toName(packet.stream_identifier());
}

[[nodiscard]] std::chrono::microseconds 
    getEndTimeInMicroSeconds(const UDataPacketImportAPI::V1::Packet &packet)
{
    auto startTimeMuSec
        = google::protobuf::util::TimeUtil::TimestampToMicroseconds(
             packet.start_time());
    auto nSamples = packet.number_of_samples();
    double samplingRate = packet.sampling_rate();
    if (samplingRate <= 0)
    {   
        throw std::invalid_argument("Sampling rate not positive");
    }   
    auto dtMuSec = static_cast<int64_t> (std::round(1000000/samplingRate));
    auto endTimeMuSec = startTimeMuSec + dtMuSec*(nSamples - 1); 
    return std::chrono::microseconds {endTimeMuSec};
}


struct DataPacketHeader
{
public:
    explicit DataPacketHeader(
        const UDataPacketImportAPI::V1::Packet &packet)
    {
        name = ::toName(packet);
#ifndef NDEBUG
        assert(!name.empty());
#endif
        // Start and end time
        auto startTimeMuS
            = google::protobuf::util::TimeUtil::TimestampToMicroseconds(
                packet.start_time());
        startTime = std::chrono::microseconds {startTimeMuS};
        endTime = ::getEndTimeInMicroSeconds(packet);
        // Sampling rate (approximate)
        samplingRate
            = static_cast<int> (std::round(packet.sampling_rate()));
        // Number of samples
        nSamples = packet.number_of_samples();
        if (nSamples <= 0)
        {
            throw std::invalid_argument("No samples in packet");
        }
    } 
    bool operator<(const ::DataPacketHeader &rhs) const
    {
        return startTime < rhs.startTime;
    } 
    bool operator>(const ::DataPacketHeader &rhs) const
    {
        return startTime > rhs.startTime;
    }
    bool operator==(const ::DataPacketHeader &rhs) const
    {
        if (rhs.name != name){return false;}
        if (rhs.samplingRate - samplingRate != 0)
        {
            throw std::runtime_error("Inconsistent sampling rates for: "
                                   + name);
            //return false;
        }
        if (rhs.nSamples != nSamples){return false;}
        auto dStartTime = std::abs(rhs.startTime.count() - startTime.count());
        if (samplingRate < 105)
        {
            return (dStartTime < std::chrono::microseconds {15000}.count());
        }
        else if (samplingRate < 255)
        {
            return (dStartTime < std::chrono::microseconds {4500}.count());
        }
        else if (samplingRate < 505)
        {
            return (dStartTime < std::chrono::microseconds {2500}.count());
        }
        else if (samplingRate < 1005)
        {
            return (dStartTime < std::chrono::microseconds {1500}.count());
        }
        throw std::runtime_error(
            "Could not classify sampling rate: " + std::to_string(samplingRate)
          + " for " + name);
        //return false;
    } 
    std::string name; // Packet name NETWORK.STATION.CHANNEL.LOCATION
    std::chrono::microseconds startTime{0}; // UTC time of first sample
    std::chrono::microseconds endTime{0}; // UTC time of last sample
    // Typically `observed' sampling rates wobble around a nominal sampling rate
    int samplingRate{100};
    int nSamples{0}; // Number of samples in packet
};

[[nodiscard]] int estimateCapacity(const ::DataPacketHeader &header,
                                   const std::chrono::seconds &memory)
{
    auto duration
        = std::max(0.0,
                   std::round( (header.nSamples - 1.)
                               /std::max(1, header.samplingRate)));
    std::chrono::seconds packetDuration{static_cast<int> (duration)};
    return std::max(10, static_cast<int> (1.5*memory.count()/duration)) + 1;
}

}

///--------------------------------------------------------------------------///

class DuplicatePacketDetectorOptions::DuplicatePacketDetectorOptionsImpl
{
public:
    std::chrono::seconds mCircularBufferDuration{300};
    int mCircularBufferSize{-1};
};

/// Constructor
DuplicatePacketDetectorOptions::DuplicatePacketDetectorOptions() :
    pImpl(std::make_unique<DuplicatePacketDetectorOptionsImpl> ()) 
{
}

/// Copy constructor
DuplicatePacketDetectorOptions::DuplicatePacketDetectorOptions(
    const DuplicatePacketDetectorOptions &options)
{
    *this = options;
}

/// Copy assignment
DuplicatePacketDetectorOptions&
DuplicatePacketDetectorOptions::operator=(
    const DuplicatePacketDetectorOptions &options)
{
    if (&options == this){return *this;}
    pImpl = std::make_unique<DuplicatePacketDetectorOptionsImpl> (*options.pImpl);
    return *this;
}

/// Move assignment
DuplicatePacketDetectorOptions&
DuplicatePacketDetectorOptions::operator=(
    DuplicatePacketDetectorOptions &&options) noexcept
{
    if (&options == this){return *this;}
    pImpl = std::move(options.pImpl);
    return *this;
}

/// Size
void DuplicatePacketDetectorOptions::setCircularBufferSize(
    const int size)
{
    if (size <= 0)
    {
        throw std::invalid_argument("Circular buffer size of " 
                                  + std::to_string(size)
                                  + " must be positive");
    }
    pImpl->mCircularBufferSize = size;
    pImpl->mCircularBufferDuration = std::chrono::seconds{-1};
}

std::optional<int>
DuplicatePacketDetectorOptions::getCircularBufferSize() const noexcept
{
    return pImpl->mCircularBufferSize > 0 ?
           std::optional<int> (pImpl->mCircularBufferSize)
           : std::nullopt;
}

/// Duration
void DuplicatePacketDetectorOptions::setCircularBufferDuration(
    const std::chrono::seconds &duration)
{
    if (duration.count() <= 0)
    {   
        throw std::invalid_argument("Duration must be positive");
    }   
    pImpl->mCircularBufferDuration = duration;
    pImpl->mCircularBufferSize =-1;
}

std::optional<std::chrono::seconds>
DuplicatePacketDetectorOptions::getCircularBufferDuration() const noexcept
{   
    return pImpl->mCircularBufferDuration.count() > 0 ?
           std::optional<std::chrono::seconds> (pImpl->mCircularBufferDuration)
           : std::nullopt;
}

/// Destructor
DuplicatePacketDetectorOptions::~DuplicatePacketDetectorOptions() = default;

///--------------------------------------------------------------------------///


class DuplicatePacketDetector::DuplicatePacketDetectorImpl
{
public:
    DuplicatePacketDetectorImpl() = default;
    DuplicatePacketDetectorImpl(const DuplicatePacketDetectorImpl &impl)
    {
        *this = impl;
    }
    [[nodiscard]] bool allow(const ::DataPacketHeader &header) const
    {
#ifndef NDEBUG
        assert(!header.name.empty());
        assert(header.nSamples > 0);
#endif
        // Does this channel exist?
        auto circularBufferIndex = mCircularBuffers.find(header.name);
        if (circularBufferIndex == mCircularBuffers.end())
        {
            int capacity = mCircularBufferSize;
            if (mEstimateCapacity)
            {
                capacity
                    = ::estimateCapacity(header,
                                         mCircularBufferDuration);
            }
/*
            spdlog::info("Creating new circular buffer for: "
                       + header.name + " with capacity: "
                       + std::to_string(capacity));
*/
            boost::circular_buffer<::DataPacketHeader>
                newCircularBuffer(capacity);
            newCircularBuffer.push_back(header);
            mCircularBuffers.insert(std::pair{header.name,
                                              std::move(newCircularBuffer)});
            // Can't be a a duplicate because its the first one
            return true;
        }
        // Now we should definitely be able to find the appropriate circular
        // buffer for this stream 
        circularBufferIndex = mCircularBuffers.find(header.name);
        if (circularBufferIndex == mCircularBuffers.end())
        {
/*
            spdlog::warn(
                "Algorithm error - circular buffer doesn't exist for: "
               + header.name);
*/
            return false;
        }
        // See if this header exists (exactly)
        auto headerIndex
            = std::find(circularBufferIndex->second.begin(),
                        circularBufferIndex->second.end(),
                        header);
        if (headerIndex != circularBufferIndex->second.end())
        {
/*
            spdlog::debug("Detected duplicate for: "
                        + header.name);
*/
            return false;
        }
        // Insert it (typically new stuff shows up)
        if (header.startTime > circularBufferIndex->second.back().endTime)
        {
/*
            spdlog::debug("Inserting " + header.name
                        + " at end of circular buffer");
*/
            circularBufferIndex->second.push_back(header);
            return true;
        }
        // If it is is really old and there's space then push to front
        if (header.endTime < circularBufferIndex->second.front().startTime)
        {
            if (!circularBufferIndex->second.full())
            {
                spdlog::debug("Inserting " + header.name 
                            + " at front of circular buffer");
                circularBufferIndex->second.push_front(header);
#ifndef NDEBUG
                assert(std::is_sorted(circularBufferIndex->second.begin(),
                                      circularBufferIndex->second.end(),
                       [](const ::DataPacketHeader &lhs, const ::DataPacketHeader &rhs)
                       {
                          return lhs.startTime < rhs.startTime;
                       }));
#endif
            }
            // Note, if the buffer is full then this packet is expired in the
            // eyes of the circular buffer.  
            return false;
        }
        // The packet is old.  We have to check for a GPS slip.
        for (const auto &streamHeader : circularBufferIndex->second)
        {
            if ((header.startTime >= streamHeader.startTime &&
                 header.startTime <= streamHeader.endTime) ||
                (header.endTime >= streamHeader.startTime &&
                 header.endTime <= streamHeader.endTime))
            {
                //std::cout << std::setprecision(16) << header.name << " " << streamHeader.name << " | " << header.startTime.count()*1.e-6 << " " << streamHeader.startTime.count()*1.e-6 << " | " 
                //<< header.endTime.count()*1.e-6 << " " << streamHeader.endTime.count()*1.e-6 << std::endl;
/*
                spdlog::info("Detected possible timing slip for: "
                           + header.name);
*/
                return false;
            }
        }
        // This appears to be a valid (out-of-order) back-fill
/*
        spdlog::debug("Inserting " + header.name
                    + " in circular buffer then sorting...");
*/
        circularBufferIndex->second.push_back(header);
        std::sort(circularBufferIndex->second.begin(),
                  circularBufferIndex->second.end(),
                  [](const ::DataPacketHeader &lhs, const ::DataPacketHeader &rhs)
                  {
                      return lhs.startTime < rhs.startTime;
                  });
        return true;
    }
    DuplicatePacketDetectorImpl& operator=(const DuplicatePacketDetectorImpl &impl)
    {
        if (&impl == this){return *this;}
        {
        std::lock_guard<std::mutex> lockGuard(impl.mMutex);
        mCircularBuffers = impl.mCircularBuffers;
        }
        mCircularBufferDuration = impl.mCircularBufferDuration;
        mCircularBufferSize = impl.mCircularBufferSize;
        mEstimateCapacity = impl.mEstimateCapacity;
        return *this;
    }
//private:
    mutable std::mutex mMutex;
    mutable std::map<std::string, boost::circular_buffer<::DataPacketHeader>>
        mCircularBuffers;
    std::chrono::seconds mCircularBufferDuration{300};
    int mCircularBufferSize{100}; // ~3s packets 
    bool mEstimateCapacity{false};
};

/// Constructor
DuplicatePacketDetector::DuplicatePacketDetector(
    const DuplicatePacketDetectorOptions &options) :
    pImpl(std::make_unique<DuplicatePacketDetectorImpl> ())
{
    auto circularBufferDuration = options.getCircularBufferDuration();
    if (circularBufferDuration != std::nullopt)
    {
        if (circularBufferDuration->count() <= 0)
        {
            throw std::invalid_argument(
               "Circular buffer duration must be positive");
        }
        pImpl->mCircularBufferDuration = *circularBufferDuration;
        pImpl->mEstimateCapacity = true;
    }
    else
    {
        auto circularBufferSize = options.getCircularBufferSize();
        if (circularBufferSize != std::nullopt)
        {
            if (*circularBufferSize <= 0)
            {
                throw std::invalid_argument(
                    "Circular buffer size must be positive");
            }
        }
        else
        {
            throw std::invalid_argument(
                "Circular buffer size or duration must be specified");
        }
        pImpl->mCircularBufferSize = *circularBufferSize;
        pImpl->mEstimateCapacity = false;
    }
}

/// Copy constructor
DuplicatePacketDetector::DuplicatePacketDetector(
    const DuplicatePacketDetector &sanitizer)
{
    *this = sanitizer;
}

/// Move constructor
DuplicatePacketDetector::DuplicatePacketDetector(
    DuplicatePacketDetector &&sanitizer) noexcept
{
    *this = std::move(sanitizer);
}

/// Copy assignment
DuplicatePacketDetector&
DuplicatePacketDetector::operator=(const DuplicatePacketDetector &sanitizer)
{
    if (&sanitizer == this){return *this;} 
    pImpl = std::make_unique<DuplicatePacketDetectorImpl> (*sanitizer.pImpl);
    return *this;
}

/// Move assignment
DuplicatePacketDetector&
DuplicatePacketDetector::operator=(DuplicatePacketDetector &&sanitizer) noexcept
{
    if (&sanitizer == this){return *this;}
    pImpl = std::move(sanitizer.pImpl);
    return *this;
}

/// Destructor
DuplicatePacketDetector::~DuplicatePacketDetector() = default;

/// Allow this packet?
bool DuplicatePacketDetector::allow(
    const UDataPacketImportAPI::V1::Packet &packet) const
{
    // Construct the trace header for the circular buffer
    std::unique_ptr<::DataPacketHeader> header{nullptr};
    try
    {
        header = std::make_unique<::DataPacketHeader> (packet);
    }
    catch (const std::exception &e)
    {
        spdlog::warn(
            "Failed to unpack dataPacketHeader.  Failed because: "
          + std::string {e.what()} + "; Not allowing...");
        return false;
    }
    return pImpl->allow(*header); // Throws
}

bool DuplicatePacketDetector::operator()(
    const UDataPacketImportAPI::V1::Packet &packet) const
{
    return allow(packet);
}

