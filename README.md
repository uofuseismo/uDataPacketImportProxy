# About

The import proxy is a common endpoint that allows various import tools to deposit seismic data packets from outside the UUSS K8s cluster.  This proxy is entirely about throughput - i.e., getting packets in and onto downstream consumers such as [uDataPacketService](https://github.com/uofuseismo/uDataPacketService).

# Proto Files
To obtain the proto files prior to compiling this software do the following:

    git subtree add --prefix uDataPacketImportAPI https://github.com/uofuseismo/uDataPacketImportAPI.git main --squash 

To update the proto files use 

    git subtree pull --prefix uDataPacketImportAPI https://github.com/uofuseismo/uDataPacketImportAPI.git main --squash

# Conan

Create a profile Linux-x86_64-clang-21

    [buildenv]
    CC=/usr/bin/clang-21
    CXX=/usr/bin/clang++-21

    [settings]
    arch=x86_64
    build_type=Release
    compiler=clang
    compiler.cppstd=20
    compiler.libcxx=libstdc++11
    compiler.version=21
    os=Linux

    [conf]
    tools.cmake.cmaketoolchain:generator=Ninja

build-missing downloads and installs missing packages
Release versions of packages (could set to Debug or other cmake build types)

    conan install . --build=missing -s build_type=Release -pr:a=Linux-x86_64-clang-21 --output-folder ./conanBuild
    cmake --preset conan-release
    cmake --build --preset conan-release
 

    
