module;

#include <atomic>
#include <string>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/metrics/meter.h>
#include <opentelemetry/metrics/meter_provider.h>
#include <opentelemetry/metrics/provider.h>
#include <opentelemetry/exporters/otlp/otlp_http.h>
#include <opentelemetry/exporters/otlp/otlp_http_metric_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_metric_exporter_options.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_options.h>
#include <opentelemetry/sdk/metrics/meter_context.h>
#include <opentelemetry/sdk/metrics/meter_context_factory.h>
#include <opentelemetry/sdk/metrics/meter_provider.h>
#include <opentelemetry/sdk/metrics/meter_provider_factory.h>
#include <opentelemetry/sdk/metrics/provider.h>
#include <opentelemetry/sdk/metrics/view/instrument_selector_factory.h>
#include <opentelemetry/sdk/metrics/view/meter_selector_factory.h>

export module metrics;
import programOptions;

namespace UDataPacketImportProxy::Metrics
{

export void initialize(const Options::ProgramOptions &programOptions)
{
    if (!programOptions.exportMetrics){return;}
    namespace otel = opentelemetry;
    otel::exporter::otlp::OtlpHttpMetricExporterOptions exporterOptions;
    exporterOptions.url = programOptions.otelHTTPMetricsOptions.url
                        + programOptions.otelHTTPMetricsOptions.suffix;
    //exporterOptions.console_debug = debug != "" && debug != "0" && debug != "no";
    exporterOptions.content_type
        = otel::exporter::otlp::HttpRequestContentType::kBinary;

    auto exporter
        = otel::exporter::otlp::OtlpHttpMetricExporterFactory::Create(
             exporterOptions);

    // Initialize and set the global MeterProvider
    otel::sdk::metrics::PeriodicExportingMetricReaderOptions readerOptions;
    readerOptions.export_interval_millis
        = programOptions.otelHTTPMetricsOptions.exportInterval;
    readerOptions.export_timeout_millis
        = programOptions.otelHTTPMetricsOptions.exportTimeOut;

    auto reader
        = otel::sdk::metrics::PeriodicExportingMetricReaderFactory::Create(
             std::move(exporter),
             readerOptions);

    auto context = otel::sdk::metrics::MeterContextFactory::Create();
    context->AddMetricReader(std::move(reader));

    auto metricsProvider
        = otel::sdk::metrics::MeterProviderFactory::Create(
             std::move(context));
    std::shared_ptr<otel::metrics::MeterProvider>
        provider(std::move(metricsProvider));

    otel::sdk::metrics::Provider::SetMeterProvider(provider);
}

export void cleanup()
{
     std::shared_ptr<opentelemetry::metrics::MeterProvider> none;
     opentelemetry::sdk::metrics::Provider::SetMeterProvider(none);
}

/*
opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
    receivedPacketsCounter;
opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
    sentPacketsCounter;
opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
    publisherUtilizationGauge;
opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
    subscriberUtilizationGauge;

std::atomic<int64_t> observableReceivedPacketsCounter{0};
std::atomic<int64_t> observableSentPacketsCounter{0};
std::atomic<double> observablePublisherUtilization{0};
std::atomic<double> observableSubscriberUtilization{0};
*/

export class MetricsSingleton
{
public:
    static MetricsSingleton &getInstance()
    {
        std::mutex mutex;
        std::scoped_lock lock{mutex};
        static MetricsSingleton instance;
        return instance;
    }
    void incrementReceivedPacketsCounter() noexcept
    {
        mReceivedPacketsCounter.fetch_add(1);
    }
    [[nodiscard]] int64_t getReceivedPacketsCount() const noexcept
    {
        return mReceivedPacketsCounter.load();
    }
    void incrementSentPacketsCounter() noexcept
    {
        mSentPacketsCounter.fetch_add(1);
    }
    [[nodiscard]] int64_t getSentPacketsCount() const noexcept
    {
        return mSentPacketsCounter.load();
    }
    void updatePublisherUtilization(const double utilization)
    {
        mPublisherUtilization.store(utilization);
    }
    [[nodiscard]] double getPublisherUtilization() const noexcept
    {
       return mPublisherUtilization.load();
    }
    void updateSubscriberUtilization(const double utilization)
    {
        mSubscriberUtilization.store(utilization);
    }
    [[nodiscard]] double getSubscriberUtilization() const noexcept
    {
       return mSubscriberUtilization.load();
    }
    MetricsSingleton(const MetricsSingleton &) = delete;
    MetricsSingleton(MetricsSingleton &&) noexcept = delete;
    MetricsSingleton& operator=(const MetricsSingleton &) = delete;
    MetricsSingleton& operator=(MetricsSingleton &&) noexcept = delete;
private:
    MetricsSingleton() = default;
    ~MetricsSingleton() = default;
    std::atomic<int64_t> mReceivedPacketsCounter{0};
    std::atomic<int64_t> mSentPacketsCounter{0};
    std::atomic<double> mPublisherUtilization{0};
    std::atomic<double> mSubscriberUtilization{0};
};

export void observeNumberOfPacketsReceived(
    opentelemetry::metrics::ObserverResult observerResult,
    void *)
{
    if (opentelemetry::nostd::holds_alternative
        <
            opentelemetry::nostd::shared_ptr
            <
                opentelemetry::metrics::ObserverResultT<int64_t>
            >
        > (observerResult))
    {
        auto observer = opentelemetry::nostd::get
        <
            opentelemetry::nostd::shared_ptr
            <
               opentelemetry::metrics::ObserverResultT<int64_t>
            >
        > (observerResult);
        try
        {
            auto &instance = MetricsSingleton::getInstance();
            auto value = instance.getReceivedPacketsCount();
            //auto value = observableReceivedPacketsCounter.load();
            observer->Observe(value);
        }
        catch (const std::exception &e)
        {

        }
    }
}

export void observeNumberOfPacketsSent(
    opentelemetry::metrics::ObserverResult observerResult,
    void *)
{
    if (opentelemetry::nostd::holds_alternative
        <
            opentelemetry::nostd::shared_ptr
            <
                opentelemetry::metrics::ObserverResultT<int64_t>
            >
        > (observerResult))
    {   
        auto observer = opentelemetry::nostd::get
        <
            opentelemetry::nostd::shared_ptr
            <
               opentelemetry::metrics::ObserverResultT<int64_t>
            >
        > (observerResult);
        try
        {
            auto &instance = MetricsSingleton::getInstance();
            auto value = instance.getSentPacketsCount();
            //auto value = observableSentPacketsCounter.load();
            observer->Observe(value);
        }
        catch (const std::exception &e) 
        {

        }
    }   
}

export void observePublisherUtilization(
    opentelemetry::metrics::ObserverResult observerResult,
    void *)
{
    if (opentelemetry::nostd::holds_alternative
        <
            opentelemetry::nostd::shared_ptr
            <
                opentelemetry::metrics::ObserverResultT<int64_t>
            >
        > (observerResult))
    {
        auto observer = opentelemetry::nostd::get
        <
            opentelemetry::nostd::shared_ptr
            <
               opentelemetry::metrics::ObserverResultT<int64_t>
            >
        > (observerResult);
        try
        {
            auto &instance = MetricsSingleton::getInstance();
            auto value = instance.getPublisherUtilization();
            //auto value = observablePublisherUtilization.load();
            observer->Observe(value);
        }
        catch (const std::exception &e)
        {

        }
    }
}

export void observeSubscriberUtilization(
    opentelemetry::metrics::ObserverResult observerResult,
    void *)
{
    if (opentelemetry::nostd::holds_alternative
        <
            opentelemetry::nostd::shared_ptr
            <
                opentelemetry::metrics::ObserverResultT<int64_t>
            >
        > (observerResult))
    {
        auto observer = opentelemetry::nostd::get
        <
            opentelemetry::nostd::shared_ptr
            <
               opentelemetry::metrics::ObserverResultT<int64_t>
            >
        > (observerResult);
        try
        {
            auto &instance = MetricsSingleton::getInstance();
            auto value = instance.getSubscriberUtilization();
            //auto value = observableSubscriberUtilization.load();
            observer->Observe(value);
        }
        catch (const std::exception &e)
        {

        }
    }
}


}

