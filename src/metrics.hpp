#ifndef UDATA_PACKET_IMPORT_PROXY_METRICS_HPP
#define UDATA_PACKET_IMPORT_PROXY_METRICS_HPP 
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
namespace
{
void initializeMetrics(const std::string &otelExporterURL)
{
    opentelemetry::exporter::otlp::OtlpHttpMetricExporterOptions exporterOptions;
    exporterOptions.url = otelExporterURL;
    //exporterOptions.console_debug = debug != "" && debug != "0" && debug != "no";
    exporterOptions.content_type
        = opentelemetry::exporter::otlp::HttpRequestContentType::kBinary;

    auto exporter
      = opentelemetry::exporter::otlp::OtlpHttpMetricExporterFactory::Create(
          exporterOptions);

    // Initialize and set the global MeterProvider
    opentelemetry::sdk::metrics::PeriodicExportingMetricReaderOptions readerOptions;
    readerOptions.export_interval_millis = std::chrono::seconds {2}; //std::chrono::milliseconds(1000);
    readerOptions.export_timeout_millis  = std::chrono::milliseconds(500);

    auto reader
        = opentelemetry::sdk::metrics::PeriodicExportingMetricReaderFactory::Create(
             std::move(exporter),
             readerOptions);

    auto context = opentelemetry::sdk::metrics::MeterContextFactory::Create();
    context->AddMetricReader(std::move(reader));

    auto metricsProvider
        = opentelemetry::sdk::metrics::MeterProviderFactory::Create(
             std::move(context));
    std::shared_ptr<opentelemetry::metrics::MeterProvider>
        provider(std::move(metricsProvider));
}

void cleanupMetrics()
{
     std::shared_ptr<opentelemetry::metrics::MeterProvider> none;
     opentelemetry::sdk::metrics::Provider::SetMeterProvider(none);
}
}

#endif
