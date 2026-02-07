#pragma once
#include <windows.h>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include "LatencyMeasurer.h"

class InputThread {
public:
    struct Config {
        bool enableTimerBoost = false;
        bool enableProcessPriority = false;
        bool enableThreadPriority = false;

        bool enableAffinity = false;
        DWORD_PTR affinityMask = 0;

        UINT timerResolutionMs = 1;
        DWORD processPriority = HIGH_PRIORITY_CLASS;
        int threadPriority = THREAD_PRIORITY_TIME_CRITICAL;
    };

    InputThread();
    ~InputThread();

    bool Start(const Config& config);
    void Stop();

    bool IsRunning() const { return running_; }
    bool ShouldBeRunning() const { return desired_running_; }

    Config GetConfig() const;
    void UpdateConfig(const Config& newConfig);

    LatencyMeasurer& GetMeasurer() { return measurer_; }
    std::wstring GetStatus() const;

private:
    void ThreadProc();
    bool CreateHiddenWindow();
    void DestroyHiddenWindow();
    bool InitializeRawInput(HWND hwnd);
    void Cleanup();

    void ApplyThreadPriority();
    void ApplyProcessPriority();
    void ApplyTimerResolution();
    void ApplyAffinity();
    void RestoreSystemSettings();

    static LRESULT CALLBACK HiddenWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    std::thread thread_;
    HANDLE thread_handle_ = nullptr;
    DWORD thread_id_ = 0;

    std::atomic<bool> running_{false};
    std::atomic<bool> should_exit_{false};
    std::atomic<bool> desired_running_{false};

    mutable std::mutex config_mutex_;
    Config config_{};
    LatencyMeasurer measurer_{};

    HANDLE hMmcss_ = nullptr;
    DWORD mmcss_task_index_ = 0;

    HINSTANCE h_instance_ = nullptr;
    HWND hwnd_ = nullptr;

    DWORD original_process_priority_ = NORMAL_PRIORITY_CLASS;
    UINT applied_timer_resolution_ms_ = 0;

    DWORD_PTR original_affinity_mask_ = 0;
    DWORD_PTR applied_affinity_mask_ = 0;
};
