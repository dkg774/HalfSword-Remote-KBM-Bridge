#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <cwchar>
#include <initializer_list>
#include "keybindings_v1_3_beta.h"

int load_bridge_into_half_sword();

namespace {
HANDLE g_mapping = nullptr;
BridgeSettings *g_settings = nullptr;
HWND g_slider = nullptr, g_value = nullptr, g_status = nullptr, g_alt_option = nullptr;
HWND g_lock_available = nullptr, g_lock_p1 = nullptr, g_lock_p2 = nullptr;
HWND g_host_g_mirror = nullptr;
HWND g_record_button = nullptr, g_record_p1 = nullptr, g_record_p2 = nullptr, g_record_both = nullptr;
HWND g_marker_enabled = nullptr, g_marker_key = nullptr, g_issue_note = nullptr;
HWND g_tab = nullptr, g_key_list = nullptr, g_key_combo = nullptr, g_binding_capture_button = nullptr;
HWND g_key_reset = nullptr, g_key_info = nullptr, g_tooltip = nullptr, g_shift_tip = nullptr;
HWND g_cadence_combo = nullptr, g_cadence_label = nullptr;
HWND g_lock_heading = nullptr, g_lock_explanation = nullptr;
HWND g_record_heading = nullptr, g_record_path = nullptr, g_marker_label = nullptr, g_issue_label = nullptr;
HWND g_main_controls[32]{};
int g_main_count = 0;
wchar_t g_ini_path[MAX_PATH]{};
ULONGLONG g_start_requested = 0;
bool g_start_worker_pending = false;
bool g_marker_capture = false;
bool g_marker_capture_ready = false;
bool g_binding_capture = false;
bool g_binding_capture_ready = false;
constexpr UINT kBridgeStartFinished = WM_APP + 1;
constexpr UINT kCapturedMarkerKey = WM_APP + 2;
constexpr UINT kCapturedBindingKey = WM_APP + 3;

void marker_key_label(LONG key, wchar_t *label, size_t length) {
    if (key == VK_NUMPAD0) { wcscpy_s(label, length, L"Numpad 0"); return; }
    const wchar_t *mouse_name = key == VK_LBUTTON ? L"Left mouse" :
        key == VK_RBUTTON ? L"Right mouse" : key == VK_MBUTTON ? L"Middle mouse" :
        key == VK_XBUTTON1 ? L"Mouse 4" : key == VK_XBUTTON2 ? L"Mouse 5" : nullptr;
    if (mouse_name) { wcscpy_s(label, length, mouse_name); return; }
    const UINT scan = MapVirtualKeyW(static_cast<UINT>(key), MAPVK_VK_TO_VSC);
    const LONG key_name = static_cast<LONG>(scan << 16) |
        ((key == VK_INSERT || key == VK_DELETE || key == VK_HOME || key == VK_END ||
          key == VK_PRIOR || key == VK_NEXT || key == VK_LEFT || key == VK_RIGHT ||
          key == VK_UP || key == VK_DOWN) ? (1L << 24) : 0);
    if (!scan || !GetKeyNameTextW(key_name, label, static_cast<int>(length)))
        swprintf_s(label, length, L"Key %ld", key);
}

void update_marker_key_button() {
    if (!g_marker_key) return;
    wchar_t name[64]{}, label[90]{};
    marker_key_label(InterlockedCompareExchange(&g_settings->issue_marker_vk, 0, 0),
        name, sizeof(name) / sizeof(name[0]));
    swprintf_s(label, L"Bind marker: %s", name);
    SetWindowTextW(g_marker_key, label);
}

DWORD WINAPI start_bridge_worker(void *context) {
    const int result = load_bridge_into_half_sword();
    PostMessageW(static_cast<HWND>(context), kBridgeStartFinished,
        static_cast<WPARAM>(result), 0);
    return 0;
}

bool event_exists(const wchar_t *name) {
    HANDLE event = OpenEventW(SYNCHRONIZE, FALSE, name);
    if (!event) return false;
    CloseHandle(event);
    return true;
}

void refresh_status() {
    if (event_exists(L"Local\\HalfSwordBridge13BetaRunning")) {
        const LONG recording = InterlockedCompareExchange(&g_settings->recording_player, 0, 0);
        SetWindowTextW(g_status, recording == 3 ? L"Running. Recording both players' input states."
            : recording == 1 ? L"Running. Recording Player 1's input states."
            : recording == 2 ? L"Running. Recording Player 2's input states."
            : L"Running. The joiner's keyboard and mouse control Player 2.");
        g_start_requested = 0;
    } else if (g_start_requested && GetTickCount64() - g_start_requested < 30000) {
        SetWindowTextW(g_status, L"Starting bridge...");
    } else if (g_start_requested) {
        SetWindowTextW(g_status, L"Start failed. Check HalfSwordBridge13Beta.log in this folder.");
        g_start_requested = 0;
    } else {
        SetWindowTextW(g_status, L"Stopped. Start Half Sword, then click Start.");
    }
}

void update_sensitivity() {
    const int percent = static_cast<int>(SendMessageW(g_slider, TBM_GETPOS, 0, 0));
    InterlockedExchange(&g_settings->sensitivity_per_thousand, percent * 10);
    wchar_t label[90]{};
    swprintf_s(label, L"Joiner mouse sensitivity: %d%%", percent);
    SetWindowTextW(g_value, label);
}

void start_bridge(HWND window) {
    if (event_exists(L"Local\\HalfSwordBridge13BetaRunning")) return;
    if (g_start_worker_pending ||
        (g_start_requested && GetTickCount64() - g_start_requested < 30000)) return;
    HANDLE start = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Local\\HalfSwordBridge13BetaStart");
    if (start) {
        SetEvent(start);
        CloseHandle(start);
        g_start_requested = GetTickCount64();
        refresh_status();
        return;
    }
    g_start_requested = GetTickCount64();
    g_start_worker_pending = true;
    refresh_status();
    HANDLE worker = CreateThread(nullptr, 0, start_bridge_worker, window, 0, nullptr);
    if (worker) CloseHandle(worker);
    else {
        g_start_worker_pending = false;
        g_start_requested = 0;
        SetWindowTextW(g_status, L"Could not start the bridge loader.");
    }
}

void stop_bridge() {
    const bool running = event_exists(L"Local\\HalfSwordBridge13BetaRunning");
    if (running) {
        // Let the bridge finish writing the recording before its stop event is set.
        const LONG request = InterlockedIncrement(&g_settings->recording_close_request);
        InterlockedExchange(&g_settings->recording_player, 0);
        SetWindowTextW(g_record_button, L"Start recording");
        SetWindowTextW(g_status, L"Saving recording before stopping bridge...");
        const ULONGLONG deadline = GetTickCount64() + 3000;
        while (InterlockedCompareExchange(&g_settings->recording_close_ack, 0, 0) != request &&
               GetTickCount64() < deadline && event_exists(L"Local\\HalfSwordBridge13BetaRunning")) {
            Sleep(10);
        }
    } else {
        InterlockedExchange(&g_settings->recording_player, 0);
    }
    HANDLE stop = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Local\\HalfSwordBridge13BetaStop");
    if (stop) {
        SetEvent(stop);
        CloseHandle(stop);
    }
    g_start_requested = 0;
    SetWindowTextW(g_status, L"Stopping bridge...");
}

void reset_inputs() {
    HANDLE reset = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Local\\HalfSwordBridge13BetaReset");
    if (reset) { SetEvent(reset); CloseHandle(reset); }
}

void update_record_button() {
    const LONG player = InterlockedCompareExchange(&g_settings->recording_player, 0, 0);
    SetWindowTextW(g_record_button, player ? L"Stop recording" : L"Start recording");
}

void toggle_recording(HWND window) {
    const LONG current = InterlockedCompareExchange(&g_settings->recording_player, 0, 0);
    if (current) InterlockedExchange(&g_settings->recording_player, 0);
    else if (event_exists(L"Local\\HalfSwordBridge13BetaRunning")) {
        GetWindowTextW(g_issue_note, g_settings->issue_note,
            static_cast<int>(sizeof(g_settings->issue_note) / sizeof(wchar_t)));
        const int player = SendMessageW(g_record_p1, BM_GETCHECK, 0, 0) == BST_CHECKED ? 1
            : SendMessageW(g_record_p2, BM_GETCHECK, 0, 0) == BST_CHECKED ? 2 : 3;
        InterlockedExchange(&g_settings->recording_player, player);
    } else MessageBoxW(window, L"Start the bridge before recording.",
        L"Half Sword joiner controls", MB_OK | MB_ICONINFORMATION);
    update_record_button();
}

void add_tip(HWND window, HWND control, const wchar_t *description) {
    if (!control || !g_tooltip) return;
    TOOLINFOW tip{};
    tip.cbSize = sizeof(tip);
    tip.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    tip.hwnd = window;
    tip.uId = reinterpret_cast<UINT_PTR>(control);
    tip.lpszText = const_cast<wchar_t *>(description);
    SendMessageW(g_tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tip));
}

BOOL CALLBACK move_main_control(HWND control, LPARAM parameter) {
    if (control == g_tab || g_main_count >= 32) return TRUE;
    g_main_controls[g_main_count++] = control;
    RECT box{};
    GetWindowRect(control, &box);
    MapWindowPoints(nullptr, reinterpret_cast<HWND>(parameter), reinterpret_cast<POINT *>(&box), 2);
    SetWindowPos(control, nullptr, box.left, box.top + 35, 0, 0,
        SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
    return TRUE;
}

void show_tab(int selected) {
    for (int i = 0; i < g_main_count; ++i)
        ShowWindow(g_main_controls[i], selected == 0 ? SW_SHOW : SW_HIDE);
    for (HWND control : {g_lock_heading, g_lock_available, g_lock_p1, g_lock_p2,
                         g_cadence_label, g_cadence_combo, g_lock_explanation})
        if (control) ShowWindow(control, selected == 1 ? SW_SHOW : SW_HIDE);
    for (HWND control : {g_record_heading, g_record_p1, g_record_p2, g_record_both,
                         g_record_button, g_record_path, g_marker_enabled, g_marker_label,
                         g_marker_key, g_issue_label, g_issue_note})
        if (control) ShowWindow(control, selected == 2 ? SW_SHOW : SW_HIDE);
    for (HWND control : {g_key_list, g_binding_capture_button, g_key_reset, g_key_info})
        if (control) ShowWindow(control, selected == 3 ? SW_SHOW : SW_HIDE);
}

void add_choice(const wchar_t *name, LONG token) {
    const LRESULT row = SendMessageW(g_key_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
    if (row >= 0) SendMessageW(g_key_combo, CB_SETITEMDATA, static_cast<WPARAM>(row), token);
}

void populate_choices() {
    for (LONG key = 4; key <= 29; ++key) {
        wchar_t label[2]{static_cast<wchar_t>(L'A' + key - 4), 0};
        add_choice(label, key);
    }
    for (LONG key = 30; key <= 39; ++key) {
        wchar_t label[2]{static_cast<wchar_t>(key == 39 ? L'0' : L'1' + key - 30), 0};
        add_choice(label, key);
    }
    const struct { const wchar_t *name; LONG code; } extras[] = {
        {L"Enter",40}, {L"Escape",41}, {L"Backspace",42}, {L"Tab",43}, {L"Space",44},
        {L"Minus",45}, {L"Equals",46}, {L"Left bracket",47}, {L"Right bracket",48},
        {L"Backslash",49}, {L"Semicolon",51}, {L"Apostrophe",52}, {L"Grave",53},
        {L"Comma",54}, {L"Period",55}, {L"Slash",56}, {L"Caps Lock",57},
        {L"Left Ctrl",224}, {L"Left Alt",226}, {L"Left Shift (reserved)",225},
        {L"Right Shift (reserved)",229},
        {L"Right arrow",79}, {L"Left arrow",80}, {L"Down arrow",81}, {L"Up arrow",82},
        {L"Numpad 0",98},
        {L"Mouse left",257}, {L"Mouse right",258}, {L"Mouse middle",272}
    };
    for (const auto &choice : extras) add_choice(choice.name, choice.code);
    for (int n = 1; n <= 12; ++n) {
        wchar_t label[8]{};
        swprintf_s(label, L"F%d", n);
        add_choice(label, 57 + n);
    }
}

const wchar_t *token_label(LONG token) {
    const int count = static_cast<int>(SendMessageW(g_key_combo, CB_GETCOUNT, 0, 0));
    for (int row = 0; row < count; ++row) {
        if (SendMessageW(g_key_combo, CB_GETITEMDATA, row, 0) != token) continue;
        static wchar_t label[80]{};
        SendMessageW(g_key_combo, CB_GETLBTEXT, row, reinterpret_cast<LPARAM>(label));
        return label;
    }
    return L"Unknown";
}

void refresh_bindings() {
    for (int row = 0; row < kBindingCount; ++row) {
        wchar_t label[80]{};
        wcscpy_s(label, token_label(InterlockedCompareExchange(&g_settings->bindings[row], 0, 0)));
        ListView_SetItemText(g_key_list, row, 1, label);
    }
}

void select_binding() {
    const int selected = ListView_GetNextItem(g_key_list, -1, LVNI_SELECTED);
    if (selected < 0 || !g_binding_capture_button || g_binding_capture) return;
    const LONG token = InterlockedCompareExchange(&g_settings->bindings[selected], 0, 0);
    wchar_t label[120]{};
    swprintf_s(label, L"Press to bind: %s", token_label(token));
    SetWindowTextW(g_binding_capture_button, label);
}

void save_bindings() {
    for (int row = 0; row < kBindingCount; ++row) {
        wchar_t key[24]{}, value[24]{};
        swprintf_s(key, L"Action%d", row);
        swprintf_s(value, L"%ld", InterlockedCompareExchange(&g_settings->bindings[row], 0, 0));
        WritePrivateProfileStringW(L"JoinerKeybinds", key, value, g_ini_path);
    }
}

void show_shift_warning() {
    if (!g_shift_tip) return;
    RECT box{};
    GetWindowRect(g_binding_capture_button, &box);
    TOOLINFOW tip{};
    tip.cbSize = sizeof(tip);
    tip.hwnd = g_binding_capture_button;
    tip.uId = 1;
    tip.lpszText = const_cast<wchar_t *>(
        L"Shift is reserved for Steam Remote Play controls and can exit or interrupt the joiner's session. Use Caps Lock for sprint or choose another key.");
    SendMessageW(g_shift_tip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tip));
    SendMessageW(g_shift_tip, TTM_TRACKPOSITION, 0, MAKELPARAM(box.left + 25, box.bottom + 3));
    SendMessageW(g_shift_tip, TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&tip));
    SetTimer(GetParent(g_binding_capture_button), 2, 4500, nullptr);
}

void assign_binding(HWND window, LONG token) {
    if (event_exists(L"Local\\HalfSwordBridge13BetaRunning")) {
        MessageBoxW(window, L"Click Stop before changing a keybind, then Start again.",
            L"Joiner keybinds", MB_OK | MB_ICONINFORMATION);
        return;
    }
    const int selected = ListView_GetNextItem(g_key_list, -1, LVNI_SELECTED);
    if (selected < 0 || !token) return;
    if (token == 225 || token == 229) { show_shift_warning(); return; }
    for (int row = 0; row < kBindingCount; ++row) {
        if (row != selected && InterlockedCompareExchange(&g_settings->bindings[row], 0, 0) == token) {
            MessageBoxW(window, L"That key is already assigned to another action. Change that action first.",
                L"Joiner keybinds", MB_OK | MB_ICONINFORMATION);
            return;
        }
    }
    InterlockedExchange(&g_settings->bindings[selected], token);
    save_bindings();
    refresh_bindings();
    select_binding();
}

void reset_bindings(HWND window) {
    if (event_exists(L"Local\\HalfSwordBridge13BetaRunning")) {
        MessageBoxW(window, L"Click Stop before resetting keybinds.",
            L"Joiner keybinds", MB_OK | MB_ICONINFORMATION);
        return;
    }
    for (int row = 0; row < kBindingCount; ++row)
        InterlockedExchange(&g_settings->bindings[row], kDefaultBindings[row]);
    save_bindings();
    refresh_bindings();
    select_binding();
}

bool load_bindings() {
    if (!GetModuleFileNameW(nullptr, g_ini_path, MAX_PATH)) return false;
    wchar_t *slash = wcsrchr(g_ini_path, L'\\');
    if (!slash) return false;
    wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - g_ini_path), L"HalfSwordJoinerControls.ini");
    LONG loaded[kBindingCount]{};
    bool valid = true;
    for (int row = 0; row < kBindingCount; ++row) {
        wchar_t key[24]{};
        swprintf_s(key, L"Action%d", row);
        loaded[row] = GetPrivateProfileIntW(L"JoinerKeybinds", key, kDefaultBindings[row], g_ini_path);
        if (loaded[row] == 225 || loaded[row] == 229 || loaded[row] < 4 ||
            (loaded[row] > 82 && loaded[row] != 98 && loaded[row] != 224 && loaded[row] != 226 &&
             loaded[row] != 257 && loaded[row] != 258 && loaded[row] != 272)) valid = false;
        for (int earlier = 0; earlier < row; ++earlier)
            if (loaded[earlier] == loaded[row]) valid = false;
    }
    for (int row = 0; row < kBindingCount; ++row)
        InterlockedExchange(&g_settings->bindings[row], valid ? loaded[row] : kDefaultBindings[row]);
    return true;
}

LONG token_from_scan(UINT scan, bool extended) {
    if (extended) {
        switch (scan) {
            case 0x4D: return 79; // right arrow
            case 0x4B: return 80; // left arrow
            case 0x50: return 81; // down arrow
            case 0x48: return 82; // up arrow
            default: return 0;
        }
    }
    constexpr unsigned char letters[26] = {
        0x1E,0x30,0x2E,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,
        0x31,0x18,0x19,0x10,0x13,0x1F,0x14,0x16,0x2F,0x11,0x2D,0x15,0x2C};
    for (int letter = 0; letter < 26; ++letter)
        if (scan == letters[letter]) return 4 + letter;
    if (scan >= 0x02 && scan <= 0x0B) return 28 + scan; // number row, 1 through 0
    if (scan >= 0x3B && scan <= 0x44) return 58 + scan - 0x3B; // F1-F10
    switch (scan) {
        case 0x01: return 41; case 0x0E: return 42; case 0x0F: return 43;
        case 0x39: return 44; case 0x0C: return 45; case 0x0D: return 46;
        case 0x1A: return 47; case 0x1B: return 48; case 0x2B: return 49;
        case 0x27: return 51; case 0x28: return 52; case 0x29: return 53;
        case 0x33: return 54; case 0x34: return 55; case 0x35: return 56;
        case 0x3A: return 57; case 0x57: return 68; case 0x58: return 69;
        case 0x2A: return 225; case 0x36: return 229;
        case 0x1D: return 224; case 0x38: return 226;
        case 0x52: return 98; // physical Numpad 0, even when Windows calls it Insert
        default: return 0;
    }
}

LRESULT CALLBACK capture_key_subclass(HWND control, UINT message, WPARAM wparam,
    LPARAM lparam, UINT_PTR id, DWORD_PTR) {
    const bool requested = id == 1 ? g_marker_capture : g_binding_capture;
    if (requested && message == WM_GETDLGCODE) return DLGC_WANTALLKEYS;
    const bool active = id == 1 ? g_marker_capture && g_marker_capture_ready
        : g_binding_capture && g_binding_capture_ready;
    if (active && (message == WM_KEYDOWN || message == WM_SYSKEYDOWN)) {
        if (!(lparam & (1LL << 30)))
            PostMessageW(GetParent(control), id == 1 ? kCapturedMarkerKey : kCapturedBindingKey,
                wparam, lparam);
        return 0;
    }
    return DefSubclassProc(control, message, wparam, lparam);
}

bool any_capture_key_down() {
    for (int key = 1; key <= 254; ++key)
        if (GetAsyncKeyState(key) & 0x8000) return true;
    return false;
}

void finish_marker_capture(HWND window) {
    g_marker_capture = g_marker_capture_ready = false;
    KillTimer(window, 3);
    update_marker_key_button();
}

void finish_binding_capture(HWND window) {
    g_binding_capture = g_binding_capture_ready = false;
    KillTimer(window, 4);
    select_binding();
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_CREATE: {
            g_tab = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE,
                10, 10, 455, 625, window, reinterpret_cast<HMENU>(20), nullptr, nullptr);
            TCITEMW page{};
            page.mask = TCIF_TEXT;
            page.pszText = const_cast<wchar_t *>(L"Controls");
            TabCtrl_InsertItem(g_tab, 0, &page);
            page.pszText = const_cast<wchar_t *>(L"Lock On");
            TabCtrl_InsertItem(g_tab, 1, &page);
            page.pszText = const_cast<wchar_t *>(L"Record");
            TabCtrl_InsertItem(g_tab, 2, &page);
            page.pszText = const_cast<wchar_t *>(L"Keybinds");
            TabCtrl_InsertItem(g_tab, 3, &page);
            CreateWindowExW(0, L"STATIC", L"Host controls for Half Sword Remote Play Together",
                WS_CHILD | WS_VISIBLE, 20, 16, 420, 24, window, nullptr, nullptr, nullptr);
            g_status = CreateWindowExW(0, L"STATIC", L"Stopped. Start Half Sword, then click Start.",
                WS_CHILD | WS_VISIBLE, 20, 47, 430, 34, window, nullptr, nullptr, nullptr);
            CreateWindowExW(0, L"BUTTON", L"Start", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                20, 91, 205, 37, window, reinterpret_cast<HMENU>(1), nullptr, nullptr);
            CreateWindowExW(0, L"BUTTON", L"Stop", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                238, 91, 205, 37, window, reinterpret_cast<HMENU>(2), nullptr, nullptr);
            g_value = CreateWindowExW(0, L"STATIC", L"Joiner mouse sensitivity: 25%",
                WS_CHILD | WS_VISIBLE, 20, 151, 330, 24, window, nullptr, nullptr, nullptr);
            g_slider = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_HORZ,
                20, 181, 420, 45, window, nullptr, nullptr, nullptr);
            SendMessageW(g_slider, TBM_SETRANGE, TRUE, MAKELPARAM(1, 200));
            const LONG raw = InterlockedCompareExchange(&g_settings->sensitivity_per_thousand, 0, 0);
            SendMessageW(g_slider, TBM_SETPOS, TRUE, raw >= 10 && raw <= 2000 ? raw / 10 : 25);
            update_sensitivity();
            g_alt_option = CreateWindowExW(0, L"BUTTON", L"Use native mouse movement while holding Alt",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 20, 229, 420, 26,
                window, reinterpret_cast<HMENU>(4), nullptr, nullptr);
            SendMessageW(g_alt_option, BM_SETCHECK,
                InterlockedCompareExchange(&g_settings->alt_native_mouse, 0, 0) ? BST_CHECKED : BST_UNCHECKED, 0);
            CreateWindowExW(0, L"BUTTON", L"Reset stuck Player 2 inputs",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 20, 280, 423, 32,
                window, reinterpret_cast<HMENU>(3), nullptr, nullptr);
            g_host_g_mirror = CreateWindowExW(0, L"BUTTON", L"Host G also holds Player 2 G",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 20, 327, 423, 23,
                window, reinterpret_cast<HMENU>(13), nullptr, nullptr);
            SendMessageW(g_host_g_mirror, BM_SETCHECK,
                InterlockedCompareExchange(&g_settings->host_g_mirrors_player2, 0, 0)
                    ? BST_CHECKED : BST_UNCHECKED, 0);
            EnumChildWindows(window, move_main_control, reinterpret_cast<LPARAM>(window));

            g_lock_heading = CreateWindowExW(0, L"STATIC", L"Custom lock-on controls",
                WS_CHILD, 20, 65, 423, 25, window, nullptr, nullptr, nullptr);
            g_lock_available = CreateWindowExW(0, L"BUTTON", L"Custom lock-on system available",
                WS_CHILD | BS_AUTOCHECKBOX, 20, 105, 423, 26,
                window, reinterpret_cast<HMENU>(10), nullptr, nullptr);
            g_lock_p1 = CreateWindowExW(0, L"BUTTON", L"Player 1 custom lock-on",
                WS_CHILD | BS_AUTOCHECKBOX, 20, 143, 205, 26,
                window, reinterpret_cast<HMENU>(11), nullptr, nullptr);
            g_lock_p2 = CreateWindowExW(0, L"BUTTON", L"Player 2 custom lock-on",
                WS_CHILD | BS_AUTOCHECKBOX, 238, 143, 205, 26,
                window, reinterpret_cast<HMENU>(12), nullptr, nullptr);
            SendMessageW(g_lock_available, BM_SETCHECK, g_settings->lockon_available ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(g_lock_p1, BM_SETCHECK, g_settings->lockon_player1 ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(g_lock_p2, BM_SETCHECK, g_settings->lockon_player2 ? BST_CHECKED : BST_UNCHECKED, 0);
            g_cadence_label = CreateWindowExW(0, L"STATIC", L"Requested update interval:",
                WS_CHILD, 20, 198, 255, 24, window, nullptr, nullptr, nullptr);
            g_cadence_combo = CreateWindowExW(0, WC_COMBOBOXW, L"",
                WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
                285, 195, 158, 150, window, reinterpret_cast<HMENU>(34), nullptr, nullptr);
            for (LONG interval : {1L, 2L, 4L, 8L, 16L}) {
                wchar_t label[24]{};
                swprintf_s(label, L"%ld ms", interval);
                const LRESULT row = SendMessageW(g_cadence_combo, CB_ADDSTRING, 0,
                    reinterpret_cast<LPARAM>(label));
                SendMessageW(g_cadence_combo, CB_SETITEMDATA, row, interval);
            }
            const LONG saved_interval = InterlockedCompareExchange(&g_settings->lockon_cadence_ms, 0, 0);
            const int cadence_row = saved_interval == 2 ? 1 : saved_interval == 4 ? 2
                : saved_interval == 8 ? 3 : saved_interval == 16 ? 4 : 0;
            SendMessageW(g_cadence_combo, CB_SETCURSEL, cadence_row, 0);
            g_lock_explanation = CreateWindowExW(0, L"STATIC",
                L"A smaller interval may feel smoother but can increase game workload. "
                L"The game may call the bridge less often than requested.",
                WS_CHILD, 20, 245, 423, 55, window, nullptr, nullptr, nullptr);

            g_record_heading = CreateWindowExW(0, L"STATIC", L"Record input states for troubleshooting",
                WS_CHILD, 20, 65, 423, 25, window, nullptr, nullptr, nullptr);
            g_record_p1 = CreateWindowExW(0, L"BUTTON", L"Player 1 (host)",
                WS_CHILD | BS_AUTORADIOBUTTON | WS_GROUP, 20, 105, 205, 24,
                window, reinterpret_cast<HMENU>(6), nullptr, nullptr);
            g_record_p2 = CreateWindowExW(0, L"BUTTON", L"Player 2 (joiner)",
                WS_CHILD | BS_AUTORADIOBUTTON, 238, 105, 205, 24,
                window, reinterpret_cast<HMENU>(7), nullptr, nullptr);
            g_record_both = CreateWindowExW(0, L"BUTTON", L"Both players (one file)",
                WS_CHILD | BS_AUTORADIOBUTTON, 20, 135, 423, 24,
                window, reinterpret_cast<HMENU>(9), nullptr, nullptr);
            SendMessageW(g_record_both, BM_SETCHECK, BST_CHECKED, 0);
            g_record_button = CreateWindowExW(0, L"BUTTON", L"Start recording",
                WS_CHILD | BS_PUSHBUTTON, 20, 174, 423, 32,
                window, reinterpret_cast<HMENU>(8), nullptr, nullptr);
            g_record_path = CreateWindowExW(0, L"STATIC", L"Logs save beside this app. No video or audio.",
                WS_CHILD, 20, 213, 423, 24, window, nullptr, nullptr, nullptr);
            g_marker_enabled = CreateWindowExW(0, L"BUTTON", L"Enable in-game issue marker",
                WS_CHILD | BS_AUTOCHECKBOX, 20, 258, 423, 24,
                window, reinterpret_cast<HMENU>(14), nullptr, nullptr);
            SendMessageW(g_marker_enabled, BM_SETCHECK, BST_UNCHECKED, 0);
            g_marker_label = CreateWindowExW(0, L"STATIC", L"Issue marker key:",
                WS_CHILD, 20, 296, 150, 24, window, nullptr, nullptr, nullptr);
            g_marker_key = CreateWindowExW(0, L"BUTTON", L"",
                WS_CHILD | BS_PUSHBUTTON,
                183, 291, 260, 29, window, reinterpret_cast<HMENU>(15), nullptr, nullptr);
            SetWindowSubclass(g_marker_key, capture_key_subclass, 1, 0);
            update_marker_key_button();
            g_issue_label = CreateWindowExW(0, L"STATIC", L"Issue / reason:",
                WS_CHILD, 20, 339, 150, 24, window, nullptr, nullptr, nullptr);
            g_issue_note = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | ES_AUTOHSCROLL,
                183, 335, 260, 29, window, reinterpret_cast<HMENU>(16), nullptr, nullptr);
            SendMessageW(g_issue_note, EM_SETLIMITTEXT, 120, 0);

            g_key_list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                WS_CHILD | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                20, 58, 423, 335, window, reinterpret_cast<HMENU>(30), nullptr, nullptr);
            ListView_SetExtendedListViewStyle(g_key_list, LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
            LVCOLUMNW column{};
            column.mask = LVCF_TEXT | LVCF_WIDTH;
            column.cx = 270;
            column.pszText = const_cast<wchar_t *>(L"Joiner action");
            ListView_InsertColumn(g_key_list, 0, &column);
            column.cx = 130;
            column.pszText = const_cast<wchar_t *>(L"Remote key");
            ListView_InsertColumn(g_key_list, 1, &column);
            g_key_combo = CreateWindowExW(0, WC_COMBOBOXW, L"",
                WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
                20, 405, 245, 250, window, reinterpret_cast<HMENU>(31), nullptr, nullptr);
            populate_choices();
            g_binding_capture_button = CreateWindowExW(0, L"BUTTON", L"Press to bind",
                WS_CHILD | BS_PUSHBUTTON, 20, 405, 423, 32,
                window, reinterpret_cast<HMENU>(32), nullptr, nullptr);
            SetWindowSubclass(g_binding_capture_button, capture_key_subclass, 2, 0);
            g_key_reset = CreateWindowExW(0, L"BUTTON", L"Restore default keybinds",
                WS_CHILD | BS_PUSHBUTTON, 20, 450, 423, 32,
                window, reinterpret_cast<HMENU>(33), nullptr, nullptr);
            g_key_info = CreateWindowExW(0, L"STATIC",
                L"Select an action, press the button, then press a key or mouse button. Stop the bridge before editing.",
                WS_CHILD, 20, 493, 423, 27, window, nullptr, nullptr, nullptr);
            for (int row = 0; row < kBindingCount; ++row) {
                LVITEMW item{};
                item.mask = LVIF_TEXT;
                item.iItem = row;
                item.pszText = const_cast<wchar_t *>(kBindingActions[row]);
                ListView_InsertItem(g_key_list, &item);
            }
            refresh_bindings();
            ListView_SetItemState(g_key_list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            select_binding();

            g_tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                WS_POPUP | TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT,
                CW_USEDEFAULT, CW_USEDEFAULT, window, nullptr, nullptr, nullptr);
            SendMessageW(g_tooltip, TTM_SETDELAYTIME, TTDT_INITIAL, 900);
            SendMessageW(g_tooltip, TTM_SETDELAYTIME, TTDT_RESHOW, 300);
            SendMessageW(g_tooltip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 15000);
            SendMessageW(g_tooltip, TTM_SETMAXTIPWIDTH, 0, 360);
            add_tip(window, g_tab, L"Controls runs the bridge; Lock On adjusts targeting; Record saves diagnostic logs; Keybinds changes Player 2 keys.");
            add_tip(window, GetDlgItem(window, 1), L"Enable the joiner's keyboard and mouse for Half Sword Player 2. Start the game first.");
            add_tip(window, GetDlgItem(window, 2), L"Disable the bridge and release Player 2 inputs. The app stays open.");
            add_tip(window, g_slider, L"Change how strongly the joiner's mouse movement affects Player 2. Test range: 1% to 200%.");
            add_tip(window, g_alt_option, L"Switch how Player 2's mouse movement is delivered during Classic Alt Thrust. Compare both modes in-game.");
            add_tip(window, GetDlgItem(window, 3), L"Release Player 2 keys and mouse buttons if one stays held unexpectedly.");
            add_tip(window, g_record_p1, L"Record the host's Player 1 game input states for troubleshooting.");
            add_tip(window, g_record_p2, L"Record the joiner's Player 2 game input states for troubleshooting.");
            add_tip(window, g_record_both, L"Record both players' input states in one paired timeline.");
            add_tip(window, g_record_button, L"Start or stop a timestamped input log beside this app. It includes bridge-side Remote Play timing, not network ping. No video or audio is recorded.");
            add_tip(window, g_marker_enabled, L"Off by default. Enable this while recording to mark the exact moment you notice lag, inverted controls, or another issue without leaving the game.");
            add_tip(window, g_marker_key, L"Default: Numpad 0. With Num Lock off, Windows also calls that physical key Insert. Click to choose another host key or mouse button. Escape cancels. Function keys are reserved. The marker works only while recording and enabled.");
            add_tip(window, g_issue_note, L"Optional description of what you are testing or the issue you notice. It is written at recording start and with each issue marker. Do not enter private information.");
            add_tip(window, g_host_g_mirror, L"When enabled, holding G as the host also holds G for Player 2. Release respects Player 2's own G state. Turn this off to leave Player 2's G independent.");
            add_tip(window, g_key_list, L"Select the joiner action you want to rebind.");
            add_tip(window, g_binding_capture_button, L"Press this button, release it, then press the desired key or mouse button. The bridge must be stopped. Duplicate keys and Shift are rejected.");
            add_tip(window, g_key_reset, L"Restore the original joiner keybinds, including Caps Lock for sprint.");
            add_tip(window, g_cadence_combo, L"Choose the requested minimum lock-on update interval. The game can call the bridge less often. You can change this while running.");
            g_shift_tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                WS_POPUP | TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT,
                CW_USEDEFAULT, CW_USEDEFAULT, window, nullptr, nullptr, nullptr);
            SendMessageW(g_shift_tip, TTM_SETMAXTIPWIDTH, 0, 340);
            TOOLINFOW shift_tip{};
            shift_tip.cbSize = sizeof(shift_tip);
            shift_tip.uFlags = TTF_TRACK | TTF_ABSOLUTE;
            shift_tip.hwnd = g_binding_capture_button;
            shift_tip.uId = 1;
            shift_tip.lpszText = const_cast<wchar_t *>(
                L"Shift is reserved for Steam Remote Play controls and can interrupt the joiner's session. Choose another key.");
            SendMessageW(g_shift_tip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&shift_tip));
            SetTimer(window, 1, 500, nullptr);
            show_tab(0);
            refresh_status();
            return 0;
        }
        case WM_HSCROLL:
            if (reinterpret_cast<HWND>(lparam) == g_slider) update_sensitivity();
            return 0;
        case WM_COMMAND:
            if (LOWORD(wparam) == 1) start_bridge(window);
            if (LOWORD(wparam) == 2) stop_bridge();
            if (LOWORD(wparam) == 3) reset_inputs();
            if (LOWORD(wparam) == 8) toggle_recording(window);
            if (LOWORD(wparam) == 10)
                InterlockedExchange(&g_settings->lockon_available,
                    SendMessageW(g_lock_available, BM_GETCHECK, 0, 0) == BST_CHECKED ? 1 : 0);
            if (LOWORD(wparam) == 11)
                InterlockedExchange(&g_settings->lockon_player1,
                    SendMessageW(g_lock_p1, BM_GETCHECK, 0, 0) == BST_CHECKED ? 1 : 0);
            if (LOWORD(wparam) == 12)
                InterlockedExchange(&g_settings->lockon_player2,
                    SendMessageW(g_lock_p2, BM_GETCHECK, 0, 0) == BST_CHECKED ? 1 : 0);
            if (LOWORD(wparam) == 13)
                InterlockedExchange(&g_settings->host_g_mirrors_player2,
                    SendMessageW(g_host_g_mirror, BM_GETCHECK, 0, 0) == BST_CHECKED ? 1 : 0);
            if (LOWORD(wparam) == 14)
                InterlockedExchange(&g_settings->issue_marker_enabled,
                    SendMessageW(g_marker_enabled, BM_GETCHECK, 0, 0) == BST_CHECKED ? 1 : 0);
            if (LOWORD(wparam) == 15 && HIWORD(wparam) == BN_CLICKED) {
                g_marker_capture = true;
                g_marker_capture_ready = false;
                SetWindowTextW(g_marker_key, L"Release, then press a button...");
                SetFocus(g_marker_key);
                SetTimer(window, 3, 25, nullptr);
            }
            if (LOWORD(wparam) == 16 && HIWORD(wparam) == EN_KILLFOCUS)
                GetWindowTextW(g_issue_note, g_settings->issue_note,
                    static_cast<int>(sizeof(g_settings->issue_note) / sizeof(wchar_t)));
            if (LOWORD(wparam) == 32 && HIWORD(wparam) == BN_CLICKED) {
                if (event_exists(L"Local\\HalfSwordBridge13BetaRunning")) {
                    MessageBoxW(window, L"Click Stop before changing a keybind, then Start again.",
                        L"Joiner keybinds", MB_OK | MB_ICONINFORMATION);
                } else {
                    g_binding_capture = true;
                    g_binding_capture_ready = false;
                    SetWindowTextW(g_binding_capture_button, L"Release, then press a key or mouse button...");
                    SetFocus(g_binding_capture_button);
                    SetTimer(window, 4, 25, nullptr);
                }
            }
            if (LOWORD(wparam) == 33) {
                if (g_binding_capture) finish_binding_capture(window);
                reset_bindings(window);
            }
            if (LOWORD(wparam) == 34 && HIWORD(wparam) == CBN_SELCHANGE) {
                const LRESULT row = SendMessageW(g_cadence_combo, CB_GETCURSEL, 0, 0);
                if (row >= 0) InterlockedExchange(&g_settings->lockon_cadence_ms,
                    static_cast<LONG>(SendMessageW(g_cadence_combo, CB_GETITEMDATA, row, 0)));
            }
            if (LOWORD(wparam) == 4)
                InterlockedExchange(&g_settings->alt_native_mouse,
                    SendMessageW(g_alt_option, BM_GETCHECK, 0, 0) == BST_CHECKED ? 1 : 0);
            return 0;
        case WM_NOTIFY: {
            const auto *notice = reinterpret_cast<const NMHDR *>(lparam);
            if (notice && notice->hwndFrom == g_tab && notice->code == TCN_SELCHANGE) {
                if (g_marker_capture) finish_marker_capture(window);
                if (g_binding_capture) finish_binding_capture(window);
                show_tab(TabCtrl_GetCurSel(g_tab));
            }
            if (notice && notice->hwndFrom == g_key_list && notice->code == LVN_ITEMCHANGED) {
                if (g_binding_capture) finish_binding_capture(window);
                select_binding();
            }
            return 0;
        }
        case WM_TIMER:
            if (wparam == 1) refresh_status();
            if (wparam == 2) {
                TOOLINFOW tip{};
                tip.cbSize = sizeof(tip);
                tip.hwnd = g_binding_capture_button;
                tip.uId = 1;
                SendMessageW(g_shift_tip, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&tip));
                KillTimer(window, 2);
            }
            if (wparam == 3 && g_marker_capture) {
                int pressed = 0;
                for (int key : {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2}) {
                    if (GetAsyncKeyState(key) & 0x8000) { pressed = key; break; }
                }
                if (!g_marker_capture_ready) {
                    if (!any_capture_key_down()) g_marker_capture_ready = true;
                } else if (pressed) {
                    InterlockedExchange(&g_settings->issue_marker_vk, pressed);
                    finish_marker_capture(window);
                }
            }
            if (wparam == 4 && g_binding_capture) {
                if (!g_binding_capture_ready) {
                    if (!any_capture_key_down()) g_binding_capture_ready = true;
                } else {
                    const LONG token = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) ? 257
                        : (GetAsyncKeyState(VK_RBUTTON) & 0x8000) ? 258
                        : (GetAsyncKeyState(VK_MBUTTON) & 0x8000) ? 272 : 0;
                    if (token) {
                        finish_binding_capture(window);
                        assign_binding(window, token);
                    }
                }
            }
            return 0;
        case kCapturedMarkerKey: {
            if (!g_marker_capture) return 0;
            const UINT scan = (static_cast<UINT_PTR>(lparam) >> 16) & 0xFF;
            const bool extended = (static_cast<UINT_PTR>(lparam) & (1ULL << 24)) != 0;
            LONG key = static_cast<LONG>(wparam);
            if (scan == 0x52 && !extended) key = VK_NUMPAD0;
            finish_marker_capture(window);
            if (key == VK_ESCAPE) return 0;
            if (key >= VK_F1 && key <= VK_F24) {
                MessageBoxW(window, L"Function keys may conflict with game or Steam shortcuts. Choose another marker button.",
                    L"Issue marker", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            InterlockedExchange(&g_settings->issue_marker_vk, key);
            update_marker_key_button();
            return 0;
        }
        case kCapturedBindingKey: {
            if (!g_binding_capture) return 0;
            const UINT scan = (static_cast<UINT_PTR>(lparam) >> 16) & 0xFF;
            const bool extended = (static_cast<UINT_PTR>(lparam) & (1ULL << 24)) != 0;
            const LONG token = token_from_scan(scan, extended);
            finish_binding_capture(window);
            if (wparam == VK_ESCAPE) return 0;
            if (!token) {
                MessageBoxW(window, L"That key is not supported for joiner controls. Choose another key.",
                    L"Joiner keybinds", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            assign_binding(window, token);
            return 0;
        }
        case kBridgeStartFinished: {
            g_start_worker_pending = false;
            const int code = static_cast<int>(wparam);
            if (event_exists(L"Local\\HalfSwordBridge13BetaRunning") || code == 0 || code == 7 || code == 8) {
                // Loading can finish shortly after the loader's wait expires.
                refresh_status();
                return 0;
            }
            g_start_requested = 0;
            const wchar_t *reason = code == 1 ? L"Start Half Sword before pressing Start."
                : code == 3 ? L"This app supports the standard Half Sword game executable. Check the game install."
                : L"Could not start the bridge. Check the game and folder contents.";
            SetWindowTextW(g_status, L"Bridge failed to start.");
            MessageBoxW(window, reason, L"Half Sword joiner controls", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        case WM_DESTROY:
            InterlockedExchange(&g_settings->recording_player, 0);
            KillTimer(window, 1);
            KillTimer(window, 3);
            KillTimer(window, 4);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\HalfSwordBridgeControlApp31Single");
    if (!single || GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"The Half Sword joiner controls are already open.",
            L"Half Sword joiner controls", MB_OK | MB_ICONINFORMATION);
        if (single) CloseHandle(single);
        return 1;
    }
    g_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
        sizeof(BridgeSettings), L"Local\\HalfSwordBridgeSettingsV13Beta");
    if (!g_mapping) { CloseHandle(single); return 2; }
    const bool created = GetLastError() != ERROR_ALREADY_EXISTS;
    g_settings = static_cast<BridgeSettings *>(MapViewOfFile(g_mapping,
        FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(BridgeSettings)));
    if (!g_settings) { CloseHandle(g_mapping); CloseHandle(single); return 3; }
    if (created) {
        InterlockedExchange(&g_settings->sensitivity_per_thousand, 250);
        InterlockedExchange(&g_settings->alt_native_mouse, 1);
        InterlockedExchange(&g_settings->recording_player, 0);
        InterlockedExchange(&g_settings->lockon_available, 1);
        InterlockedExchange(&g_settings->lockon_player1, 1);
        InterlockedExchange(&g_settings->lockon_player2, 1);
        InterlockedExchange(&g_settings->host_g_mirrors_player2, 1);
        InterlockedExchange(&g_settings->issue_marker_enabled, 0);
        InterlockedExchange(&g_settings->issue_marker_vk, VK_NUMPAD0);
        InterlockedExchange(&g_settings->lockon_cadence_ms, 1);
    }
    if (!load_bindings()) { UnmapViewOfFile(g_settings); CloseHandle(g_mapping); CloseHandle(single); return 3; }
    InterlockedExchange(&g_settings->recording_player, 0);
    InterlockedExchange(&g_settings->issue_marker_enabled, 0);
    InterlockedExchange(&g_settings->issue_marker_vk, VK_NUMPAD0);
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_BAR_CLASSES | ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&common);
    WNDCLASSW kind{};
    kind.lpfnWndProc = window_proc;
    kind.hInstance = instance;
    kind.lpszClassName = L"HalfSwordBridgeControlApp19";
    kind.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    kind.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    RegisterClassW(&kind);
    HWND window = CreateWindowExW(0, kind.lpszClassName, L"Half Sword joiner controls 1.3 Lock On Unstable Beta",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 475, 675, nullptr, nullptr, instance, nullptr);
    if (window) {
        ShowWindow(window, show);
        UpdateWindow(window);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    UnmapViewOfFile(g_settings);
    CloseHandle(g_mapping);
    ReleaseMutex(single);
    CloseHandle(single);
    return window ? 0 : 4;
}
