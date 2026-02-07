#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>

class AutoStartManager {
public:
    AutoStartManager();
    ~AutoStartManager() = default;

    bool EnableAutoStart(const std::wstring& appPath, const std::wstring& appName);
    bool DisableAutoStart(const std::wstring& appName);
    bool IsAutoStartEnabled(const std::wstring& appName) const;

    void CleanupAllEntries(const std::wstring& appName);

private:
    bool EnableViaRegistry(const std::wstring& appPath, const std::wstring& appName);
    bool DisableViaRegistry(const std::wstring& appName);
    bool IsEnabledViaRegistry(const std::wstring& appName) const;

    bool EnableViaTaskScheduler(const std::wstring& appPath, const std::wstring& appName);
    bool DisableViaTaskScheduler(const std::wstring& appName);
    bool IsEnabledViaTaskScheduler(const std::wstring& appName) const;

    std::wstring GetExecutablePath() const;
};
