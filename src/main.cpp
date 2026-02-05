import getNow;
import logger;
import programOptions;
import metrics;
#include <iostream>
#include <atomic>
#include <mutex>
#include <csignal>
#ifndef NDEBUG
#include <cassert>
#endif
#include <filesystem>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <opentelemetry/metrics/meter_provider.h>
#include <opentelemetry/metrics/provider.h>
#include <boost/program_options.hpp>
//#include "programOptions.hpp"
#include "proxy.hpp"
#include "proxyOptions.hpp"
//#include "otelSpdlogSink.hpp"
//#include "metrics.hpp"
//#include "logger.hpp"

namespace
{
std::atomic<bool> mInterrupted{false};

opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
    receivedPacketsCounter;
opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
    sentPacketsCounter;
opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
    publisherUtilizationGauge;
opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
    subscriberUtilizationGauge;

[[nodiscard]] std::pair<std::string, bool> parseCommandLineOptions(int, char *[]);

class ServerImpl
{
public:
    explicit ServerImpl(const UDataPacketImportProxy::Options::ProgramOptions &options,
                        std::shared_ptr<spdlog::logger> logger,
                        UDataPacketImportProxy::Metrics::MetricsSingleton *metrics) :
        mLogger(logger),
        mMetrics(metrics)
    {
#ifndef NDEBUG
        assert(mLogger != nullptr);
        assert(mMetrics != nullptr);
#endif
        mProxy
           = std::make_unique<UDataPacketImportProxy::Proxy>
             (options.proxyOptions, mLogger, mMetrics); 
        // Metrics
        if (options.exportMetrics)
        {
            // Need a provider from which to get a meter.  This is initialized
            // once and should last the duration of the application.
            auto provider 
                = opentelemetry::metrics::Provider::GetMeterProvider();
     
            // Meter will be bound to application (library, module, class, etc.)
            // so as to identify who is genreating these metrics.
            auto meter = provider->GetMeter(options.applicationName, "1.2.0");

            // Packets received
            receivedPacketsCounter
                = meter->CreateInt64ObservableCounter(
                  "seismic_data.import.grpc_proxy.client.consumed.packets",
                  "Number of packets received from telemetry by import proxy",
                  "{packet}");
            receivedPacketsCounter->AddCallback(
                UDataPacketImportProxy::Metrics::observeNumberOfPacketsReceived,
                nullptr);

            sentPacketsCounter
                = meter->CreateInt64ObservableCounter(
                  "seismic_data.import.grpc_proxy.client.sent.packets",
                  "Number of packets sent to from import proxy backend to subscribers",
                  "{packet}");
            sentPacketsCounter->AddCallback(
                UDataPacketImportProxy::Metrics::observeNumberOfPacketsSent,
                nullptr);

            publisherUtilizationGauge
                = meter->CreateDoubleObservableGauge(
                  "seismic_data.import.grpc_proxy.client.utilization",
                  "Proportion of publishers submitting packets to the proxy frontend",
                  "");
            publisherUtilizationGauge->AddCallback(
                UDataPacketImportProxy::Metrics::observePublisherUtilization,
                nullptr);

            subscriberUtilizationGauge
                = meter->CreateDoubleObservableGauge(
                  "seismic_data.import.grpc_proxy.server.utilization",
                  "Proportion of subscribers receiving packets from the proxy backend",
                  "");
            subscriberUtilizationGauge->AddCallback(
                UDataPacketImportProxy::Metrics::observeSubscriberUtilization,
                nullptr);

        }
    }

    /// Start the proxy service
    void start()
    {
#ifndef NDEBUG
        assert(mProxy != nullptr);
#endif
        stop();
        std::this_thread::sleep_for (std::chrono::milliseconds {10});
        mKeepRunning = true;
        auto proxyFutures = mProxy->start();
        for (auto &p : proxyFutures){mFutures.push_back(std::move(p));}
        handleMainThread();
    }

    /// Stop the proxy service
    void stop()
    {
        mKeepRunning = false;
        if (mProxy){mProxy->stop();}
        for (auto &future : mFutures)
        { 
            if (future.valid()){future.get();}
        }
    }

    /// Check futures
    [[nodiscard]]
    bool checkFuturesOkay(const std::chrono::milliseconds &timeOut)
    {           
        bool isOkay{true};
        for (auto &future : mFutures)
        {           
            try     
            {
                auto status = future.wait_for(timeOut);
                if (status == std::future_status::ready)
                {
                    future.get();
                }
            }
            catch (const std::exception &e)
            {
                SPDLOG_LOGGER_CRITICAL(mLogger,
                                       "Fatal error in SEEDLink import: {}",
                                       std::string {e.what()});
                isOkay = false;
            }
        }
        return isOkay;
    }

    // Print some summary statistics
    void printSummary()
    {
        if (mMetrics == nullptr){return;}
        if (mOptions.printSummaryInterval.count() <= 0){return;}
        auto now = UDataPacketImportProxy::Utilities::getNow();
        if (now > mLastPrintSummary + mOptions.printSummaryInterval)
        {
            mLastPrintSummary = now;

            auto nPublishers = mProxy->getNumberOfPublishers();
            auto nSubscribers = mProxy->getNumberOfSubscribers(); 
            auto nReceived = mMetrics->getReceivedPacketsCount();
            auto nSent = mMetrics->getSentPacketsCount();
            auto nPacketsReceived = nReceived - mReportNumberOfPacketsReceived;
            auto nPacketsSent = nSent - mReportNumberOfPacketsSent;
            mReportNumberOfPacketsReceived = nReceived;
            mReportNumberOfPacketsSent = nSent;
            SPDLOG_LOGGER_INFO(mLogger,
                               "Current number of publishers {}.  Current number of subscribers {}.  Packets received since last report {}.  Packets sent since last report {}.",
                               nPublishers,
                               nSubscribers,
                               nPacketsReceived,
                               nPacketsSent);
        } 
    } 

    // Calling thread from Run gets stuck here then fails through to
    // destructor
    void handleMainThread()
    {
        SPDLOG_LOGGER_DEBUG(mLogger, "Main thread entering waiting loop");
        catchSignals();
        {
            while (!mStopRequested)
            {
                if (mInterrupted)
                {
                    SPDLOG_LOGGER_INFO(mLogger,
                                       "SIGINT/SIGTERM signal received!");
                    mStopRequested = true;
                    break;
                }
                printSummary();
                if (!checkFuturesOkay(std::chrono::milliseconds {5}))
                {   
                    SPDLOG_LOGGER_CRITICAL(
                       mLogger,
                       "Futures exception caught; terminating app");
                    mStopRequested = true;
                    break;
                }   
                std::unique_lock<std::mutex> lock(mStopMutex);
                mStopCondition.wait_for(lock,
                                        std::chrono::milliseconds {100},
                                        [this]
                                        {
                                              return mStopRequested;
                                        });
                lock.unlock();
            }
        }
        if (mStopRequested)
        {
            SPDLOG_LOGGER_DEBUG(mLogger, "Stop request received.  Exiting...");
            stop();
        }
    }

    /// Handles sigterm and sigint
    static void signalHandler(const int )
    {   
        mInterrupted = true;
    }    

    static void catchSignals()
    {   
        struct sigaction action;
        action.sa_handler = signalHandler;
        action.sa_flags = 0;  
        sigemptyset(&action.sa_mask);
        sigaction(SIGINT,  &action, NULL);
        sigaction(SIGTERM, &action, NULL);
    }   

//private:
    mutable std::mutex mStopMutex;
    UDataPacketImportProxy::Options::ProgramOptions mOptions;
    std::shared_ptr<spdlog::logger> mLogger{nullptr};
    UDataPacketImportProxy::Metrics::MetricsSingleton *mMetrics{nullptr};
    std::vector<std::future<void>> mFutures;
    std::unique_ptr<UDataPacketImportProxy::Proxy> mProxy{nullptr};
    std::condition_variable mStopCondition;
    std::chrono::microseconds mLastPrintSummary
    {
        UDataPacketImportProxy::Utilities::getNow()
    };
    int64_t mReportNumberOfPacketsReceived{0};
    int64_t mReportNumberOfPacketsSent{0};
    bool mStopRequested{false};
    std::atomic<bool> mKeepRunning{true};
};

}

int main(int argc, char *argv[])
{
    // Get the ini file from the command line
    std::filesystem::path iniFile;
    try 
    {   
        auto [iniFileName, isHelp] = ::parseCommandLineOptions(argc, argv);
        if (isHelp){return EXIT_SUCCESS;}
        iniFile = iniFileName;
    }   
    catch (const std::exception &e) 
    {   
        spdlog::critical(e.what());
        return EXIT_FAILURE;
    }   

    UDataPacketImportProxy::Options::ProgramOptions programOptions;
    try
    {
        programOptions = UDataPacketImportProxy::Options::parseIniFile(iniFile);
    } 
    catch (const std::exception &e)
    {
        spdlog::error(e.what());
        return EXIT_FAILURE;
    }
    constexpr int overwrite{1};
    setenv("OTEL_SERVICE_NAME",
           programOptions.applicationName.c_str(),
           overwrite);

    //auto logger = ::initializeLogger(programOptions);
    //::setVerbosityForSPDLOG(programOptions.verbosity, &*logger);
    auto logger = UDataPacketImportProxy::Logger::initialize(programOptions);
    auto metrics = &UDataPacketImportProxy::Metrics::MetricsSingleton::getInstance();
    try
    {
        if (programOptions.exportMetrics)
        {
            SPDLOG_LOGGER_INFO(logger, "Initializing metrics");
            UDataPacketImportProxy::Metrics::initialize(programOptions);
        }
    }
    catch (const std::exception &e)
    {
        SPDLOG_LOGGER_CRITICAL(logger,
                               "Failed to initialize metrics because {}",
                               std::string {e.what()});
        if (programOptions.exportLogs)
        {
            UDataPacketImportProxy::Logger::cleanup();
        }
        return EXIT_FAILURE;
    }
 
    try
    {
        ::ServerImpl server{programOptions, logger, metrics};
        server.start();
        if (programOptions.exportMetrics)
        {
            UDataPacketImportProxy::Metrics::cleanup();
        }
        if (programOptions.exportLogs)
        {
            UDataPacketImportProxy::Logger::cleanup();
        }
    }
    catch (const std::exception &e)
    {
        SPDLOG_LOGGER_CRITICAL(logger, "Proxy service exited with error {}",
                               std::string {e.what()});
        if (programOptions.exportMetrics)
        {
            UDataPacketImportProxy::Metrics::cleanup();
        }
        if (programOptions.exportLogs)
        {
             UDataPacketImportProxy::Logger::cleanup();
        }
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

///--------------------------------------------------------------------------///
///                            Utility Functions                             ///
///--------------------------------------------------------------------------///
namespace
{   
    
/*
void setVerbosityForSPDLOG(const int verbosity,
                           spdlog::logger *logger)
{
#ifndef NDEBUG
    assert(logger != nullptr);
#endif
    if (verbosity <= 1)
    {
        logger->set_level(spdlog::level::critical);
    }
    if (verbosity == 2){logger->set_level(spdlog::level::warn);}
    if (verbosity == 3){logger->set_level(spdlog::level::info);}
    if (verbosity >= 4){logger->set_level(spdlog::level::debug);}
}
*/

/// Read the program options from the command line
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

 
}
