# About

The import proxy is a common endpoint that allows various import tools to deposit seismic data packets from outside the UUSS K8s cluster.  This proxy is entirely about throughput - i.e., getting packets in and onto downstream consumers such as [uDataPacket](https://github.com/uofuseismo/uDataPacketService).

# Proto Files
To obtain the proto files prior to compiling this software do the following:

    git subtree add --prefix uDataPacketImportAPI https://github.com/uofuseismo/uDataPacketImportAPI.git main --squash 

To update the proto files use 

    git subtree pull --prefix uDataPacketImportAPI https://github.com/uofuseismo/uDataPacketImportAPI.git main --squash

