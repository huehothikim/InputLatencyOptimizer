#pragma once
#include <windows.h>
#include "InputThread.h"
#include "SettingsDialog.h"

struct DeviceProfile {
    bool hasBattery = false;
    bool onBattery = false;
    DWORD logicalProcessors = 0;
    DWORD activeProcessorGroups = 0;
    DWORD cpuMHz = 0;
    ULONGLONG ramBytes = 0;
    UINT timerMinMs = 1;
    UINT timerMaxMs = 15;
    DWORD_PTR processAffinityMask = 0;
    DWORD_PTR systemAffinityMask = 0;
};

struct CalibrationResult {
    bool measured = false;

    bool timerBoostHelps = false;
    double sleepP95OvershootUs_NoBoost = 0.0;
    double sleepP95OvershootUs_Boost = 0.0;

    bool affinityHelps = false;
    DWORD_PTR bestAffinityMask = 0;
    double sleepP95OvershootUs_BestCore = 0.0;
    double sleepP95OvershootUs_DefaultCore = 0.0;
};

class DeviceTuner {
public:
    static DeviceProfile CollectProfile();
    static CalibrationResult Calibrate(const DeviceProfile& p);

    static void EnsureCached();
    static const DeviceProfile& Profile();
    static const CalibrationResult& Calibration();

    static InputThread::Config ComputeConfig(SettingsDialog::Mode mode,
                                            const DeviceProfile& p,
                                            const CalibrationResult& c);

    static InputThread::Config ComputeConfigCached(SettingsDialog::Mode mode);

    // Keep performance-first but never violate current power state safety.
    static InputThread::Config NormalizeForCurrentState(const InputThread::Config& cfg);

private:
    static DWORD ReadCpuMHzFromRegistry();
    static void ReadTimerCaps(UINT& minMs, UINT& maxMs);
    static double MeasureSleepP95OvershootUs(int iterations);

    static DWORD_PTR LowestBit(DWORD_PTR mask);
    static DWORD_PTR HighestBit(DWORD_PTR mask);
    static double MeasureSleepP95OnMask(DWORD_PTR mask, int iterations);
};
