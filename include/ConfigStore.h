#pragma once
#include <windows.h>
#include "InputThread.h"

struct StoredConfig {
    bool enabled = false;
    DWORD mode = 0;        // selected mode
    DWORD appliedMode = 0; // last applied mode
    bool hasAppliedConfig = false;
    InputThread::Config appliedConfig{};
};

class ConfigStore {
public:
    static bool Load(StoredConfig& out);

    static void SaveSelectedMode(DWORD mode);
    static void SaveEnabled(bool enabled);

    static void SaveApplied(DWORD appliedMode, const InputThread::Config& cfg, bool enabled);

    // Helpers
    static bool LoadApplied(InputThread::Config& cfgOut, DWORD& appliedModeOut);
};
