#include "../include/AutoStartManager.h"
#include <taskschd.h>
#include <comdef.h>
#include <comutil.h>

#pragma comment(lib, "taskschd.lib")
#pragma comment(lib, "comsuppw.lib")

AutoStartManager::AutoStartManager() {}

bool AutoStartManager::EnableAutoStart(const std::wstring& appPath, const std::wstring& appName) {
    if (EnableViaRegistry(appPath, appName)) return true;
    return EnableViaTaskScheduler(appPath, appName);
}

bool AutoStartManager::DisableAutoStart(const std::wstring& appName) {
    bool a = DisableViaRegistry(appName);
    bool b = DisableViaTaskScheduler(appName);
    return a || b;
}

bool AutoStartManager::IsAutoStartEnabled(const std::wstring& appName) const {
    return IsEnabledViaRegistry(appName) || IsEnabledViaTaskScheduler(appName);
}

void AutoStartManager::CleanupAllEntries(const std::wstring& appName) {
    DisableViaRegistry(appName);
    DisableViaTaskScheduler(appName);
}

bool AutoStartManager::EnableViaRegistry(const std::wstring& appPath, const std::wstring& appName) {
    HKEY hKey{};
    LONG r = RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_WRITE, &hKey);

    if (r != ERROR_SUCCESS) return false;

    r = RegSetValueExW(hKey, appName.c_str(), 0, REG_SZ,
        reinterpret_cast<const BYTE*>(appPath.c_str()),
        static_cast<DWORD>((appPath.size() + 1) * sizeof(wchar_t)));

    RegCloseKey(hKey);
    return r == ERROR_SUCCESS;
}

bool AutoStartManager::DisableViaRegistry(const std::wstring& appName) {
    HKEY hKey{};
    LONG r = RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_WRITE, &hKey);

    if (r != ERROR_SUCCESS) return false;

    r = RegDeleteValueW(hKey, appName.c_str());
    RegCloseKey(hKey);

    return r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND;
}

bool AutoStartManager::IsEnabledViaRegistry(const std::wstring& appName) const {
    HKEY hKey{};
    LONG r = RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_READ, &hKey);

    if (r != ERROR_SUCCESS) return false;

    DWORD type = 0;
    DWORD size = 0;
    r = RegQueryValueExW(hKey, appName.c_str(), nullptr, &type, nullptr, &size);
    RegCloseKey(hKey);

    return r == ERROR_SUCCESS && type == REG_SZ && size > 0;
}

bool AutoStartManager::EnableViaTaskScheduler(const std::wstring& appPath, const std::wstring& appName) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) return false;

    bool ok = false;
    ITaskService* pService = nullptr;
    ITaskFolder* pRoot = nullptr;
    IRegisteredTask* pRegTask = nullptr;

    do {
        hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
            IID_ITaskService, reinterpret_cast<void**>(&pService));
        if (FAILED(hr)) break;

        hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
        if (FAILED(hr)) break;

        hr = pService->GetFolder(_bstr_t(L"\\"), &pRoot);
        if (FAILED(hr)) break;

        pRoot->DeleteTask(_bstr_t(appName.c_str()), 0);

        ITaskDefinition* pTask = nullptr;
        hr = pService->NewTask(0, &pTask);
        if (FAILED(hr)) break;

        IRegistrationInfo* pRegInfo = nullptr;
        if (SUCCEEDED(pTask->get_RegistrationInfo(&pRegInfo))) {
            pRegInfo->put_Author(_bstr_t(L"Input Latency Optimizer"));
            pRegInfo->Release();
        }

        IPrincipal* pPrincipal = nullptr;
        if (SUCCEEDED(pTask->get_Principal(&pPrincipal))) {
            pPrincipal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);
            pPrincipal->put_RunLevel(TASK_RUNLEVEL_LUA);
            pPrincipal->Release();
        }

        ITaskSettings* pSettings = nullptr;
        if (SUCCEEDED(pTask->get_Settings(&pSettings))) {
            pSettings->put_StartWhenAvailable(VARIANT_TRUE);
            pSettings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
            pSettings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
            pSettings->put_ExecutionTimeLimit(_bstr_t(L"PT0S"));
            pSettings->put_AllowHardTerminate(VARIANT_TRUE);
            pSettings->Release();
        }

        ITriggerCollection* pTriggers = nullptr;
        if (SUCCEEDED(pTask->get_Triggers(&pTriggers))) {
            ITrigger* pTrigger = nullptr;
            if (SUCCEEDED(pTriggers->Create(TASK_TRIGGER_LOGON, &pTrigger))) {
                ILogonTrigger* pLogon = nullptr;
                if (SUCCEEDED(pTrigger->QueryInterface(IID_ILogonTrigger, reinterpret_cast<void**>(&pLogon)))) {
                    pLogon->put_Enabled(VARIANT_TRUE);
                    pLogon->Release();
                }
                pTrigger->Release();
            }
            pTriggers->Release();
        }

        IActionCollection* pActions = nullptr;
        if (SUCCEEDED(pTask->get_Actions(&pActions))) {
            IAction* pAction = nullptr;
            if (SUCCEEDED(pActions->Create(TASK_ACTION_EXEC, &pAction))) {
                IExecAction* pExec = nullptr;
                if (SUCCEEDED(pAction->QueryInterface(IID_IExecAction, reinterpret_cast<void**>(&pExec)))) {
                    pExec->put_Path(_bstr_t(appPath.c_str()));
                    pExec->Release();
                }
                pAction->Release();
            }
            pActions->Release();
        }

        _variant_t user; user.vt = VT_EMPTY;
        _variant_t pass; pass.vt = VT_EMPTY;

        hr = pRoot->RegisterTaskDefinition(
            _bstr_t(appName.c_str()),
            pTask,
            TASK_CREATE_OR_UPDATE,
            user,
            pass,
            TASK_LOGON_INTERACTIVE_TOKEN,
            user,
            &pRegTask);

        ok = SUCCEEDED(hr);
        if (pTask) pTask->Release();
    } while (false);

    if (pRegTask) pRegTask->Release();
    if (pRoot) pRoot->Release();
    if (pService) pService->Release();

    CoUninitialize();
    return ok;
}

bool AutoStartManager::DisableViaTaskScheduler(const std::wstring& appName) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) return false;

    bool ok = false;
    ITaskService* pService = nullptr;
    ITaskFolder* pRoot = nullptr;

    do {
        hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
            IID_ITaskService, reinterpret_cast<void**>(&pService));
        if (FAILED(hr)) break;

        hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
        if (FAILED(hr)) break;

        hr = pService->GetFolder(_bstr_t(L"\\"), &pRoot);
        if (FAILED(hr)) break;

        hr = pRoot->DeleteTask(_bstr_t(appName.c_str()), 0);
        ok = SUCCEEDED(hr) || hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    } while (false);

    if (pRoot) pRoot->Release();
    if (pService) pService->Release();

    CoUninitialize();
    return ok;
}

bool AutoStartManager::IsEnabledViaTaskScheduler(const std::wstring& appName) const {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) return false;

    bool exists = false;
    ITaskService* pService = nullptr;
    ITaskFolder* pRoot = nullptr;
    IRegisteredTask* pTask = nullptr;

    do {
        hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
            IID_ITaskService, reinterpret_cast<void**>(&pService));
        if (FAILED(hr)) break;

        hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
        if (FAILED(hr)) break;

        hr = pService->GetFolder(_bstr_t(L"\\"), &pRoot);
        if (FAILED(hr)) break;

        hr = pRoot->GetTask(_bstr_t(appName.c_str()), &pTask);
        exists = SUCCEEDED(hr);
    } while (false);

    if (pTask) pTask->Release();
    if (pRoot) pRoot->Release();
    if (pService) pService->Release();

    CoUninitialize();
    return exists;
}

std::wstring AutoStartManager::GetExecutablePath() const {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return std::wstring(path);
}
