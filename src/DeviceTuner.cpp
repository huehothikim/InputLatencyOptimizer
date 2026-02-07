#include "../include/DeviceTuner.h"
#include <mmsystem.h>
#include <algorithm>
#include <vector>
#include <mutex>

#pragma comment(lib, "winmm.lib")

static double QpcUs() {
    static LARGE_INTEGER freq{};
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    return (c.QuadPart * 1000000.0) / static_cast<double>(freq.QuadPart);
}

static std::mutex g_cacheMutex;
static bool g_cached = false;
static DeviceProfile g_profile{};
static CalibrationResult g_calib{};

DWORD_PTR DeviceTuner::LowestBit(DWORD_PTR mask) { return mask & (~mask + 1); }

DWORD_PTR DeviceTuner::HighestBit(DWORD_PTR mask) {
    if (!mask) return 0;
    DWORD_PTR hb = 1;
    while (mask >>= 1) hb <<= 1;
    return hb;
}

void DeviceTuner::EnsureCached() {
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    if (g_cached) return;
    g_profile = CollectProfile();
    g_calib = Calibrate(g_profile);
    g_cached = true;
}

const DeviceProfile& DeviceTuner::Profile() {
    EnsureCached();
    return g_profile;
}

const CalibrationResult& DeviceTuner::Calibration() {
    EnsureCached();
    return g_calib;
}

DeviceProfile DeviceTuner::CollectProfile() {
    DeviceProfile p{};

    SYSTEM_POWER_STATUS ps{};
    if (GetSystemPowerStatus(&ps)) {
        p.hasBattery = (ps.BatteryFlag != 128);
        p.onBattery = (ps.ACLineStatus == 0);
    }

    p.activeProcessorGroups = GetActiveProcessorGroupCount();
    p.logicalProcessors = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);

    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) p.ramBytes = ms.ullTotalPhys;

    p.cpuMHz = ReadCpuMHzFromRegistry();
    ReadTimerCaps(p.timerMinMs, p.timerMaxMs);

    if (p.timerMinMs == 0) p.timerMinMs = 1;
    if (p.timerMaxMs == 0) p.timerMaxMs = 15;

    GetProcessAffinityMask(GetCurrentProcess(), &p.processAffinityMask, &p.systemAffinityMask);

    return p;
}

double DeviceTuner::MeasureSleepP95OvershootUs(int iterations) {
    std::vector<double> overs;
    overs.reserve(iterations);

    for (int i = 0; i < iterations; i++) {
        double t0 = QpcUs();
        Sleep(1);
        double t1 = QpcUs();

        double dt = t1 - t0;
        double over = dt - 1000.0;
        if (over < 0) over = 0;
        overs.push_back(over);
    }

    std::sort(overs.begin(), overs.end());
    size_t idx = static_cast<size_t>(std::ceil(0.95 * overs.size())) - 1;
    if (idx >= overs.size()) idx = overs.size() - 1;
    return overs[idx];
}

double DeviceTuner::MeasureSleepP95OnMask(DWORD_PTR mask, int iterations) {
    HANDLE th = GetCurrentThread();
    DWORD_PTR prev = 0;
    if (mask) {
        prev = SetThreadAffinityMask(th, mask);
        if (prev == 0) return MeasureSleepP95OvershootUs(iterations);
    }

    double p95 = MeasureSleepP95OvershootUs(iterations);

    if (mask && prev) SetThreadAffinityMask(th, prev);
    return p95;
}

CalibrationResult DeviceTuner::Calibrate(const DeviceProfile& p) {
    CalibrationResult r{};

    if (p.onBattery) {
        r.measured = true;
        r.timerBoostHelps = false;
        r.affinityHelps = false;
        return r;
    }

    const int iters = 48;

    r.sleepP95OvershootUs_DefaultCore = MeasureSleepP95OvershootUs(iters);
    r.sleepP95OvershootUs_NoBoost = r.sleepP95OvershootUs_DefaultCore;

    UINT reqMs = std::max<UINT>(1, p.timerMinMs);

    timeBeginPeriod(reqMs);
    r.sleepP95OvershootUs_Boost = MeasureSleepP95OvershootUs(iters);
    timeEndPeriod(reqMs);

    const double improveTimer = r.sleepP95OvershootUs_NoBoost - r.sleepP95OvershootUs_Boost;
    r.timerBoostHelps = (improveTimer >= 500.0);

    DWORD_PTR avail = p.processAffinityMask ? p.processAffinityMask : p.systemAffinityMask;
    DWORD_PTR low = LowestBit(avail);
    DWORD_PTR high = HighestBit(avail);

    if (low && high && low != high) {
        double p95Low = MeasureSleepP95OnMask(low, iters);
        double p95High = MeasureSleepP95OnMask(high, iters);

        if (p95Low <= p95High) {
            r.bestAffinityMask = low;
            r.sleepP95OvershootUs_BestCore = p95Low;
        } else {
            r.bestAffinityMask = high;
            r.sleepP95OvershootUs_BestCore = p95High;
        }

        const double improveAff = r.sleepP95OvershootUs_DefaultCore - r.sleepP95OvershootUs_BestCore;
        r.affinityHelps = (improveAff >= 300.0);
    }

    r.measured = true;
    return r;
}

InputThread::Config DeviceTuner::ComputeConfig(SettingsDialog::Mode mode,
                                              const DeviceProfile& p,
                                              const CalibrationResult& c) {
    InputThread::Config cfg{};

    const ULONGLONG ramGB = p.ramBytes / (1024ull * 1024ull * 1024ull);
    const bool strongCPU = (p.logicalProcessors >= 8) || (p.cpuMHz >= 3200);
    const UINT safeTimerMs = std::max<UINT>(1, p.timerMinMs);

    if (p.onBattery) {
        cfg.enableTimerBoost = false;
        cfg.enableProcessPriority = false;
        cfg.enableThreadPriority = false;
        cfg.enableAffinity = false;
        cfg.timerResolutionMs = safeTimerMs;
        cfg.processPriority = HIGH_PRIORITY_CLASS;
        cfg.threadPriority = THREAD_PRIORITY_HIGHEST;
        return cfg;
    }

    switch (mode) {
    case SettingsDialog::Mode::Light:
        cfg.enableThreadPriority = true;
        cfg.threadPriority = THREAD_PRIORITY_HIGHEST;
        cfg.enableProcessPriority = false;

        cfg.enableTimerBoost = false;
        cfg.timerResolutionMs = safeTimerMs;

        cfg.enableAffinity = false;
        break;

    case SettingsDialog::Mode::Medium:
        cfg.enableThreadPriority = true;
        cfg.threadPriority = THREAD_PRIORITY_HIGHEST;
        cfg.enableProcessPriority = false;

        cfg.enableTimerBoost = (c.measured && c.timerBoostHelps);
        cfg.timerResolutionMs = safeTimerMs;

        cfg.enableAffinity = (c.measured && c.affinityHelps && c.bestAffinityMask != 0);
        cfg.affinityMask = c.bestAffinityMask;
        break;

    case SettingsDialog::Mode::Max:
        cfg.enableThreadPriority = true;
        cfg.threadPriority = THREAD_PRIORITY_TIME_CRITICAL;

        cfg.enableTimerBoost = (c.measured && c.timerBoostHelps);
        cfg.timerResolutionMs = safeTimerMs;

        cfg.enableAffinity = (c.measured && c.affinityHelps && c.bestAffinityMask != 0);
        cfg.affinityMask = c.bestAffinityMask;

        cfg.enableProcessPriority = (strongCPU && ramGB >= 8 && cfg.enableTimerBoost);
        cfg.processPriority = HIGH_PRIORITY_CLASS;

        if (p.logicalProcessors <= 4) cfg.threadPriority = THREAD_PRIORITY_HIGHEST;
        break;
    }

    cfg.timerResolutionMs = std::max<UINT>(cfg.timerResolutionMs, safeTimerMs);
    return cfg;
}

InputThread::Config DeviceTuner::ComputeConfigCached(SettingsDialog::Mode mode) {
    EnsureCached();
    return ComputeConfig(mode, g_profile, g_calib);
}

InputThread::Config DeviceTuner::NormalizeForCurrentState(const InputThread::Config& inCfg) {
    DeviceProfile p = CollectProfile();
    InputThread::Config cfg = inCfg;

    if (p.onBattery) {
        cfg.enableTimerBoost = false;
        cfg.enableAffinity = false;
        cfg.enableProcessPriority = false;
        if (cfg.threadPriority == THREAD_PRIORITY_TIME_CRITICAL) cfg.threadPriority = THREAD_PRIORITY_HIGHEST;
    }

    // Never below timer min if boost enabled
    if (cfg.enableTimerBoost) {
        cfg.timerResolutionMs = std::max<UINT>(cfg.timerResolutionMs, std::max<UINT>(1, p.timerMinMs));
    }

    // Affinity must be within current process affinity
    if (cfg.enableAffinity && cfg.affinityMask) {
        DWORD_PTR procMask = 0, sysMask = 0;
        GetProcessAffinityMask(GetCurrentProcess(), &procMask, &sysMask);
        DWORD_PTR allowed = procMask ? procMask : sysMask;
        if ((cfg.affinityMask & allowed) == 0) {
            cfg.enableAffinity = false;
            cfg.affinityMask = 0;
        }
    }

    return cfg;
}

DWORD DeviceTuner::ReadCpuMHzFromRegistry() {
    HKEY hKey{};
    DWORD mhz = 0;
    DWORD sz = sizeof(mhz);

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {

        RegQueryValueExW(hKey, L"~MHz", nullptr, nullptr, reinterpret_cast<LPBYTE>(&mhz), &sz);
        RegCloseKey(hKey);
    }

    return mhz;
}

void DeviceTuner::ReadTimerCaps(UINT& minMs, UINT& maxMs) {
    TIMECAPS tc{};
    if (timeGetDevCaps(&tc, sizeof(tc)) == TIMERR_NOERROR) {
        minMs = tc.wPeriodMin;
        maxMs = tc.wPeriodMax;
    } else {
        minMs = 1;
        maxMs = 15;
    }
}
