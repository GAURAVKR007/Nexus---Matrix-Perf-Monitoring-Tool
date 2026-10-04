#include "SystemMetrics.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

// winsock2.h must come before windows.h.
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>
#include <dxgi.h>
#include <winhttp.h>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
    constexpr double BYTES_PER_GB =
        1024.0 * 1024.0 * 1024.0;

    std::string wideToUtf8(const wchar_t *value)
    {
        if (!value || !*value)
            return {};

        const int length = WideCharToMultiByte(
            CP_UTF8,
            0,
            value,
            -1,
            nullptr,
            0,
            nullptr,
            nullptr);

        if (length <= 1)
            return {};

        std::string result(
            static_cast<size_t>(length),
            '\0');

        WideCharToMultiByte(
            CP_UTF8,
            0,
            value,
            -1,
            result.data(),
            length,
            nullptr,
            nullptr);

        if (!result.empty() && result.back() == '\0')
            result.pop_back();

        return result;
    }

    std::string getLocalIPv4()
    {
        ULONG bufferSize = 0;

        const ULONG flags =
            GAA_FLAG_SKIP_ANYCAST |
            GAA_FLAG_SKIP_MULTICAST |
            GAA_FLAG_SKIP_DNS_SERVER;

        DWORD result = GetAdaptersAddresses(
            AF_INET,
            flags,
            nullptr,
            nullptr,
            &bufferSize);

        if (result != ERROR_BUFFER_OVERFLOW || bufferSize == 0)
            return "Unavailable";

        std::vector<unsigned char> buffer(bufferSize);

        auto *addresses =
            reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data());

        result = GetAdaptersAddresses(
            AF_INET,
            flags,
            nullptr,
            addresses,
            &bufferSize);

        if (result != NO_ERROR)
            return "Unavailable";

        for (auto *adapter = addresses;
             adapter != nullptr;
             adapter = adapter->Next)
        {
            if (adapter->OperStatus != IfOperStatusUp)
                continue;

            if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
                continue;

            for (auto *unicast = adapter->FirstUnicastAddress;
                 unicast != nullptr;
                 unicast = unicast->Next)
            {
                if (!unicast->Address.lpSockaddr ||
                    unicast->Address.lpSockaddr->sa_family != AF_INET)
                {
                    continue;
                }

                const auto *address =
                    reinterpret_cast<const sockaddr_in *>(
                        unicast->Address.lpSockaddr);

                char ip[INET_ADDRSTRLEN]{};

                if (InetNtopA(
                        AF_INET,
                        &address->sin_addr,
                        ip,
                        sizeof(ip)))
                {
                    return ip;
                }
            }
        }

        return "Unavailable";
    }

    std::string getHostName()
    {
        char buffer[MAX_COMPUTERNAME_LENGTH + 1]{};
        DWORD size = sizeof(buffer);

        if (GetComputerNameA(buffer, &size))
            return std::string(buffer, size);

        return "Unavailable";
    }

    std::string getUptime()
    {
        const ULONGLONG seconds = GetTickCount64() / 1000ULL;

        const ULONGLONG days = seconds / 86400ULL;
        const ULONGLONG hours = (seconds % 86400ULL) / 3600ULL;
        const ULONGLONG minutes = (seconds % 3600ULL) / 60ULL;
        const ULONGLONG secs = seconds % 60ULL;

        std::string result;

        if (days > 0)
            result += std::to_string(days) + "d ";

        if (hours > 0 || days > 0)
            result += std::to_string(hours) + "h ";

        result += std::to_string(minutes) + "m ";
        result += std::to_string(secs) + "s";

        return result;
    }

    void updatePower(SystemMetrics::Snapshot &snapshot)
    {
        SYSTEM_POWER_STATUS status{};

        if (!GetSystemPowerStatus(&status))
        {
            snapshot.powerState = "UNKNOWN";
            snapshot.batteryPercent = -1;
            return;
        }

        if (status.ACLineStatus == 1)
            snapshot.powerState = "AC POWER";
        else if (status.ACLineStatus == 0)
            snapshot.powerState = "BATTERY";
        else
            snapshot.powerState = "UNKNOWN";

        snapshot.batteryPercent =
            status.BatteryLifePercent <= 100
                ? static_cast<int>(status.BatteryLifePercent)
                : -1;
    }

    void updateStorage(SystemMetrics::Snapshot &snapshot)
    {
        DWORD drives = GetLogicalDrives();

        if (drives == 0)
            return;

        double totalGB = 0.0;
        double usedGB = 0.0;

        for (int index = 0; index < 26; ++index)
        {
            if ((drives & (1u << index)) == 0)
                continue;

            wchar_t root[] = {
                static_cast<wchar_t>(L'A' + index),
                L':',
                L'\\',
                L'\0'};

            if (GetDriveTypeW(root) != DRIVE_FIXED)
                continue;

            ULARGE_INTEGER freeBytesAvailable{};
            ULARGE_INTEGER totalBytes{};
            ULARGE_INTEGER freeBytes{};

            if (!GetDiskFreeSpaceExW(
                    root,
                    &freeBytesAvailable,
                    &totalBytes,
                    &freeBytes))
            {
                continue;
            }

            const double total =
                static_cast<double>(totalBytes.QuadPart) /
                BYTES_PER_GB;

            const double free =
                static_cast<double>(freeBytes.QuadPart) /
                BYTES_PER_GB;

            if (total <= 0.0)
                continue;

            totalGB += total;
            usedGB += std::max(0.0, total - free);
        }

        snapshot.storageTotalGB = totalGB;
        snapshot.storageUsedGB = usedGB;

        if (totalGB > 0.0)
        {
            snapshot.storageUsage = std::clamp(
                usedGB / totalGB * 100.0,
                0.0,
                100.0);
        }
    }

    void updateGPU(SystemMetrics::Snapshot &snapshot)
    {
        IDXGIFactory1 *factory = nullptr;

        if (FAILED(CreateDXGIFactory1(
                __uuidof(IDXGIFactory1),
                reinterpret_cast<void **>(&factory))))
        {
            return;
        }

        UINT64 bestVRAM = 0;
        std::string bestName = "Unavailable";

        for (UINT index = 0;; ++index)
        {
            IDXGIAdapter1 *adapter = nullptr;

            const HRESULT result =
                factory->EnumAdapters1(index, &adapter);

            if (result == DXGI_ERROR_NOT_FOUND)
                break;

            if (FAILED(result) || !adapter)
                continue;

            DXGI_ADAPTER_DESC1 desc{};

            if (SUCCEEDED(adapter->GetDesc1(&desc)))
            {
                const bool software =
                    (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;

                if (!software &&
                    desc.DedicatedVideoMemory >= bestVRAM)
                {
                    bestVRAM = desc.DedicatedVideoMemory;
                    bestName = wideToUtf8(desc.Description);

                    const std::string prefix = "NVIDIA GeForce ";

                    if (bestName.rfind(prefix, 0) == 0)
                    {
                        bestName.erase(0, prefix.length());
                    }
                }
            }

            adapter->Release();
        }

        factory->Release();

        snapshot.gpuName = bestName;
        snapshot.gpuMemoryGB =
            static_cast<double>(bestVRAM) / BYTES_PER_GB;
    }

    std::string fetchPublicIP()
    {
        HINTERNET session = WinHttpOpen(
            L"MatrixMonitor/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0);

        if (!session)
            return "Unavailable";

        WinHttpSetTimeouts(session, 1000, 1000, 1500, 1500);

        HINTERNET connection = WinHttpConnect(
            session,
            L"api.ipify.org",
            INTERNET_DEFAULT_HTTPS_PORT,
            0);

        if (!connection)
        {
            WinHttpCloseHandle(session);
            return "Unavailable";
        }

        HINTERNET request = WinHttpOpenRequest(
            connection,
            L"GET",
            L"/?format=text",
            nullptr,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE);

        if (!request)
        {
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            return "Unavailable";
        }

        const BOOL sent = WinHttpSendRequest(
            request,
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0);

        BOOL received = FALSE;

        if (sent)
            received = WinHttpReceiveResponse(request, nullptr);

        std::string result;

        if (received)
        {
            for (;;)
            {
                DWORD available = 0;

                if (!WinHttpQueryDataAvailable(
                        request,
                        &available) ||
                    available == 0)
                {
                    break;
                }

                available = std::min<DWORD>(available, 64);

                std::string buffer(
                    static_cast<size_t>(available),
                    '\0');

                DWORD read = 0;

                if (!WinHttpReadData(
                        request,
                        buffer.data(),
                        available,
                        &read) ||
                    read == 0)
                {
                    break;
                }

                result.append(buffer.data(), read);

                if (result.size() >= 64)
                    break;
            }
        }

        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);

        while (!result.empty())
        {
            const char c = result.back();

            if (c == '\r' || c == '\n' || c == ' ' || c == '\t')
                result.pop_back();
            else
                break;
        }

        return result.empty() ? "Unavailable" : result;
    }
}

SystemMetrics::SystemMetrics() = default;

void SystemMetrics::update()
{
    // CPU -----------------------------------------------------------
    FILETIME idleTime{};
    FILETIME kernelTime{};
    FILETIME userTime{};

    if (GetSystemTimes(
            &idleTime,
            &kernelTime,
            &userTime))
    {
        ULARGE_INTEGER idle{};
        ULARGE_INTEGER kernel{};
        ULARGE_INTEGER user{};

        idle.LowPart = idleTime.dwLowDateTime;
        idle.HighPart = idleTime.dwHighDateTime;

        kernel.LowPart = kernelTime.dwLowDateTime;
        kernel.HighPart = kernelTime.dwHighDateTime;

        user.LowPart = userTime.dwLowDateTime;
        user.HighPart = userTime.dwHighDateTime;

        const auto currentIdle = idle.QuadPart;
        const auto currentKernel = kernel.QuadPart;
        const auto currentUser = user.QuadPart;

        if (initialized)
        {
            const auto idleDelta = currentIdle - previousIdle;
            const auto kernelDelta = currentKernel - previousKernel;
            const auto userDelta = currentUser - previousUser;
            const auto totalDelta = kernelDelta + userDelta;

            if (totalDelta > 0)
            {
                const auto busyDelta =
                    totalDelta > idleDelta
                        ? totalDelta - idleDelta
                        : 0ULL;

                snapshot.cpuUsage = std::clamp(
                    100.0 * static_cast<double>(busyDelta) /
                        static_cast<double>(totalDelta),
                    0.0,
                    100.0);
            }
        }

        previousIdle = currentIdle;
        previousKernel = currentKernel;
        previousUser = currentUser;
        initialized = true;
    }

    // RAM -----------------------------------------------------------
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);

    if (GlobalMemoryStatusEx(&memory))
    {
        snapshot.memoryUsage = std::clamp(
            static_cast<double>(memory.dwMemoryLoad),
            0.0,
            100.0);
    }

    // Identity / network ------------------------------------------
    snapshot.localIP = getLocalIPv4();
    snapshot.hostName = getHostName();
    snapshot.uptime = getUptime();

    // Power ---------------------------------------------------------
    updatePower(snapshot);

    // Storage / GPU ------------------------------------------------
    updateStorage(snapshot);
    updateGPU(snapshot);

    // Public IP -----------------------------------------------------
    // External network lookup is intentionally rate-limited. If it
    // fails, keep the previous successful value instead of replacing
    // it with garbage.
    const unsigned long long nowMs = GetTickCount64();

    if (lastPublicIPQueryMs == 0 ||
        nowMs - lastPublicIPQueryMs >= 60000ULL)
    {
        const std::string publicIP = fetchPublicIP();

        if (publicIP != "Unavailable")
            snapshot.publicIP = publicIP;

        lastPublicIPQueryMs = nowMs;
    }
}

const SystemMetrics::Snapshot &SystemMetrics::getSnapshot() const
{
    return snapshot;
}
