#include "../include/ConfigStore.h"

static const wchar_t* kRegPath = L"Software\\InputLatencyOptimizer";

static bool ReadDWORD(HKEY hKey, const wchar_t* name, DWORD& out) {
    DWORD sz = sizeof(out);
    return RegQueryValueExW(hKey, name, nullptr, nullptr, reinterpret_cast<LPBYTE>(&out), &sz) == ERROR_SUCCESS;
}

static bool ReadQWORD(HKEY hKey, const wchar_t* name, ULONGLONG& out) {
    DWORD sz = sizeof(out);
    DWORD type = 0;
    if (RegQueryValueExW(hKey, name, nullptr, &type, reinterpret_cast<LPBYTE>(&out), &sz) != ERROR_SUCCESS) return false;
    return type == REG_QWORD;
}

static void WriteDWORD(HKEY hKey, const wchar_t* name, DWORD v) {
    RegSetValueExW(hKey, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof(v));
}

static void WriteQWORD(HKEY hKey, const wchar_t* name, ULONGLONG v) {
    RegSetValueExW(hKey, name, 0, REG_QWORD, reinterpret_cast<const BYTE*>(&v), sizeof(v));
}

bool ConfigStore::Load(StoredConfig& out) {
    HKEY hKey{};
    LONG r = RegOpenKeyExW(HKEY_CURRENT_USER, kRegPath, 0, KEY_READ, &hKey);
    if (r != ERROR_SUCCESS) return false;

    DWORD enabled = 0;
    ReadDWORD(hKey, L"Enabled", enabled);
    out.enabled = (enabled != 0);

    DWORD mode = 0;
    ReadDWORD(hKey, L"Mode", mode);
    out.mode = mode;

    DWORD appliedMode = 0;
    ReadDWORD(hKey, L"AppliedMode", appliedMode);
    out.appliedMode = appliedMode;

    DWORD hasCfg = 0;
    ReadDWORD(hKey, L"A_HasConfig", hasCfg);
    out.hasAppliedConfig = (hasCfg != 0);

    if (out.hasAppliedConfig) {
        DWORD v = 0;

        ReadDWORD(hKey, L"A_TimerBoost", v);
        out.appliedConfig.enableTimerBoost = (v != 0);

        ReadDWORD(hKey, L"A_TimerMs", v);
        out.appliedConfig.timerResolutionMs = (UINT)v;

        ReadDWORD(hKey, L"A_ProcEnable", v);
        out.appliedConfig.enableProcessPriority = (v != 0);

        ReadDWORD(hKey, L"A_ProcPrio", v);
        out.appliedConfig.processPriority = v ? v : HIGH_PRIORITY_CLASS;

        ReadDWORD(hKey, L"A_ThrEnable", v);
        out.appliedConfig.enableThreadPriority = (v != 0);

        ReadDWORD(hKey, L"A_ThrPrio", v);
        out.appliedConfig.threadPriority = (int)v;

        ReadDWORD(hKey, L"A_AffEnable", v);
        out.appliedConfig.enableAffinity = (v != 0);

        ULONGLONG q = 0;
        if (ReadQWORD(hKey, L"A_AffMask", q)) out.appliedConfig.affinityMask = (DWORD_PTR)q;
        else out.appliedConfig.affinityMask = 0;
    }

    RegCloseKey(hKey);
    return true;
}

void ConfigStore::SaveSelectedMode(DWORD mode) {
    HKEY hKey{};
    LONG r = RegCreateKeyExW(HKEY_CURRENT_USER, kRegPath, 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr);
    if (r != ERROR_SUCCESS) return;
    WriteDWORD(hKey, L"Mode", mode);
    RegCloseKey(hKey);
}

void ConfigStore::SaveEnabled(bool enabled) {
    HKEY hKey{};
    LONG r = RegCreateKeyExW(HKEY_CURRENT_USER, kRegPath, 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr);
    if (r != ERROR_SUCCESS) return;
    WriteDWORD(hKey, L"Enabled", enabled ? 1 : 0);
    RegCloseKey(hKey);
}

void ConfigStore::SaveApplied(DWORD appliedMode, const InputThread::Config& cfg, bool enabled) {
    HKEY hKey{};
    LONG r = RegCreateKeyExW(HKEY_CURRENT_USER, kRegPath, 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr);
    if (r != ERROR_SUCCESS) return;

    WriteDWORD(hKey, L"Enabled", enabled ? 1 : 0);
    WriteDWORD(hKey, L"Mode", appliedMode);
    WriteDWORD(hKey, L"AppliedMode", appliedMode);

    WriteDWORD(hKey, L"A_HasConfig", 1);

    WriteDWORD(hKey, L"A_TimerBoost", cfg.enableTimerBoost ? 1 : 0);
    WriteDWORD(hKey, L"A_TimerMs", (DWORD)cfg.timerResolutionMs);

    WriteDWORD(hKey, L"A_ProcEnable", cfg.enableProcessPriority ? 1 : 0);
    WriteDWORD(hKey, L"A_ProcPrio", (DWORD)cfg.processPriority);

    WriteDWORD(hKey, L"A_ThrEnable", cfg.enableThreadPriority ? 1 : 0);
    WriteDWORD(hKey, L"A_ThrPrio", (DWORD)cfg.threadPriority);

    WriteDWORD(hKey, L"A_AffEnable", cfg.enableAffinity ? 1 : 0);
    WriteQWORD(hKey, L"A_AffMask", (ULONGLONG)cfg.affinityMask);

    RegCloseKey(hKey);
}

bool ConfigStore::LoadApplied(InputThread::Config& cfgOut, DWORD& appliedModeOut) {
    StoredConfig s{};
    if (!Load(s)) return false;
    if (!s.hasAppliedConfig) return false;
    cfgOut = s.appliedConfig;
    appliedModeOut = s.appliedMode;
    return true;
}
