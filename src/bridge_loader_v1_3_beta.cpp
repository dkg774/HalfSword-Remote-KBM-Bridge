#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <cwchar>
#include <cstdio>

#ifndef BRIDGE_DLL_NAME
#define BRIDGE_DLL_NAME L"HalfSwordBridge13Beta.dll"
#endif

namespace {
struct TargetWindow {
    DWORD pid = 0;
    DWORD thread_id = 0;
    HWND window = nullptr;
};

DWORD find_half_sword() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W entry{sizeof(entry)};
    DWORD found = 0;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"HalfSwordUE5-Win64-Shipping.exe") != 0) continue;
            if (found) { found = 0; break; }
            found = entry.th32ProcessID;
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

BOOL CALLBACK find_target_window(HWND window, LPARAM parameter) {
    auto *target = reinterpret_cast<TargetWindow *>(parameter);
    DWORD pid = 0;
    const DWORD thread = GetWindowThreadProcessId(window, &pid);
    if (pid == target->pid && IsWindowVisible(window) && GetWindow(window, GW_OWNER) == nullptr) {
        target->window = window;
        target->thread_id = thread;
        return FALSE;
    }
    return TRUE;
}
}

int load_bridge_into_half_sword() {
    TargetWindow target{};
    target.pid = find_half_sword();
    if (!target.pid) {
        std::fwprintf(stderr, L"Start one Half Sword game process first.\n");
        return 1;
    }

    // Request only the right required to confirm that the selected process is
    // the standard Half Sword executable. No remote memory or remote threads.
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, target.pid);
    if (!process) return 2;
    wchar_t process_path[MAX_PATH]{};
    DWORD path_length = MAX_PATH;
    const bool valid_path = QueryFullProcessImageNameW(process, 0, process_path, &path_length) &&
        wcsstr(process_path,
            L"\\Half Sword\\HalfswordUE5\\Binaries\\Win64\\HalfSwordUE5-Win64-Shipping.exe") != nullptr;
    CloseHandle(process);
    if (!valid_path) return 3;

    EnumWindows(find_target_window, reinterpret_cast<LPARAM>(&target));
    if (!target.window || !target.thread_id) return 4;

    wchar_t dll_path[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, dll_path, MAX_PATH)) return 5;
    wchar_t *slash = wcsrchr(dll_path, L'\\');
    if (!slash || (slash - dll_path) + 1 + wcslen(BRIDGE_DLL_NAME) >= MAX_PATH) return 5;
    wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - dll_path), BRIDGE_DLL_NAME);

    // Load locally only to obtain the exported hook address. DllMain checks the
    // process name, so the bridge worker starts only inside Half Sword.
    HMODULE bridge = LoadLibraryW(dll_path);
    if (!bridge) return 6;
    auto callback = reinterpret_cast<HOOKPROC>(GetProcAddress(bridge, "HalfSwordBootstrapHook"));
    if (!callback) { FreeLibrary(bridge); return 6; }

    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, L"Local\\HalfSwordBridge13BetaReady");
    HHOOK hook = SetWindowsHookExW(WH_CALLWNDPROC, callback, bridge, target.thread_id);
    if (!ready || !hook) {
        if (hook) UnhookWindowsHookEx(hook);
        if (ready) CloseHandle(ready);
        FreeLibrary(bridge);
        return 7;
    }

    DWORD_PTR ignored = 0;
    SendMessageTimeoutW(target.window, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 2000, &ignored);
    // Slower hosts can finish loading after the former five-second deadline,
    // which produced a false failure dialog even though the bridge then ran.
    const DWORD wait = WaitForSingleObject(ready, 15000);
    UnhookWindowsHookEx(hook);
    CloseHandle(ready);
    FreeLibrary(bridge);
    return wait == WAIT_OBJECT_0 ? 0 : 8;
}
