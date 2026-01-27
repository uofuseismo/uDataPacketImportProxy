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
#include <boost/program_options.hpp>
#include "programOptions.hpp"
#include "proxy.hpp"
#include "proxyOptions.hpp"
#include "otelSpdlogSink.hpp"

namespace
{
std::atomic<bool> mInterrupted{false};

void setVerbosityForSPDLOG(int, std::shared_ptr<spdlog::logger> &);
[[nodiscard]] std::pair<std::string, bool> parseCommandLineOptions(int, char *[]);

class ServerImpl
{
public:
    explicit ServerImpl(const ::ProgramOptions &options,
                        std::shared_ptr<spdlog::logger> &logger) :
        mLogger(logger)
    {
#ifndef NDEUBG
        assert(mLogger != nullptr);
#endif
        mProxy
           = std::make_unique<UDataPacketImportProxy::Proxy>
             (options.proxyOptions, mLogger); 
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
        mFutures.push_back(mProxy->start());
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
    ::ProgramOptions mOptions;        
    std::shared_ptr<spdlog::logger> mLogger{nullptr};
    std::vector<std::future<void>> mFutures;
    std::unique_ptr<UDataPacketImportProxy::Proxy> mProxy{nullptr};
    std::condition_variable mStopCondition;
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

    ::ProgramOptions programOptions;
    try
    {
        programOptions = ::parseIniFile(iniFile);
    } 
    catch (const std::exception &e)
    {
        spdlog::error(e.what());
        return EXIT_FAILURE;
    }

    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt> (); 
    auto    logger
            = std::make_shared<spdlog::logger>
              (spdlog::logger ("", {consoleSink}));
    ::setVerbosityForSPDLOG(programOptions.verbosity, logger);
 
    try
    {
        ::ServerImpl server{programOptions, logger};
        server.start();
    }
    catch (const std::exception &e)
    {
        SPDLOG_LOGGER_CRITICAL(logger, "Proxy service exited with error {}",
                               std::string {e.what()});
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

///--------------------------------------------------------------------------///
///                            Utility Functions                             ///
///--------------------------------------------------------------------------///
namespace
{   
    
void setVerbosityForSPDLOG(const int verbosity,
                           std::shared_ptr<spdlog::logger> &logger)
{
    if (verbosity <= 1)
    {
        logger->set_level(spdlog::level::critical);
    }
    if (verbosity == 2){logger->set_level(spdlog::level::warn);}
    if (verbosity == 3){logger->set_level(spdlog::level::info);}
    if (verbosity >= 4){logger->set_level(spdlog::level::debug);}
}

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
