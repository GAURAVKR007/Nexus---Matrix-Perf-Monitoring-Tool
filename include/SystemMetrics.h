#pragma once

#include <string>

class SystemMetrics
{
public:
    struct Snapshot
    {
        double cpuUsage = 0.0;
        double memoryUsage = 0.0;

        std::string localIP = "Unavailable";
        std::string publicIP = "Unavailable";
        std::string hostName = "Unavailable";
        std::string uptime = "0m 0s";
        std::string powerState = "UNKNOWN";
        int batteryPercent = -1;

        double storageUsedGB = 0.0;
        double storageTotalGB = 0.0;
        double storageUsage = 0.0;

        std::string gpuName = "Unavailable";
        double gpuMemoryGB = 0.0;
    };

    SystemMetrics();
    void update();
    const Snapshot& getSnapshot() const;

private:
    Snapshot snapshot;

    unsigned long long previousIdle = 0;
    unsigned long long previousKernel = 0;
    unsigned long long previousUser = 0;
    bool initialized = false;

    unsigned long long lastPublicIPQueryMs = 0;
};
