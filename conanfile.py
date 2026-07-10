from conan import ConanFile
from conan.tools.build import can_run
from conan.tools.cmake import cmake_layout, CMake, CMakeDeps, CMakeToolchain

class uDataPacketImportProxyConan(ConanFile):
   name = "uDataPacketImportProxy"
   version = "0.0.5"
   license = "MIT"
   description = "Serves as a common endpoint for various external seismic data packet ingestion mechanisms in the UUSS Kubernetes environment."
   url = "https://github.com/uofuseismo/uDataPacketImportProxy"
   topics = ("uDataPacketImportProxy")
   settings = "os", "compiler", "build_type", "arch"
   default_options = {"hwloc/*:shared": "True",
                      "opentelemetry-cpp/*:with_otlp_http": "True",
                      "opentelemetry-cpp/*:with_otlp_grpc": "True",
                      "opentelemetry-cpp/*:with_abi_v2" : "True",
                      "spdlog/*:header_only" : "True",}
   export_sources = "CMakeLists.txt", "LICENSE", "README.md", "cmake/*", "src/*", "testing/*"
   generators = "CMakeDeps", "CMakeToolchain"

   def requirements(self):
       self.requires("grpc/1.78.1")
       self.requires("opentelemetry-cpp/1.24.0")
       self.requires("protobuf/6.33.5")
       self.requires("boost/1.89.0")
       self.requires("spdlog/1.17.0")
       self.requires("onetbb/2022.3.0")

   def build_requirements(self):
       self.test_requires("catch2/3.13.0")

   def layout(self):
       cmake_layout(self)

   def build(self):
       cmake = CMake(self)
       cmake.configure()
       cmake.build()

   def test(self):
       if can_run(self):
          cmake.test()

   #def generate(self):
   #    tc = CMakeToolchain(self)
   #    tc.generate()

   def package(self):
       cmake = CMake(self)
       cmake.install()

   def package_info(self):
       self.cpp_info.libs = ["uDataPacketImportProxy"]

       #self.cpp_info.components["otlp_http_metric_exporter"].libs = ["exporters_otlp_http"] #opentelemetry_exporter_otlp_http_log"]
       #self.cpp_info.components["otlp_http_metric_exporter"].set_property("cmake_target_name", "opentelemetry-cpp::otlp_http_log_record_exporter")

       #self.cpp_info.components["otlp_http_metric_exporter"].libs = ["exporters_otlp_http"] #opentelemetry_exporter_otlp_http_metric"]
       #self.cpp_info.components["otlp_http_metric_exporter"].set_property("cmake_target_name", "opentelemetry-cpp::otlp_http_metric_exporter")
