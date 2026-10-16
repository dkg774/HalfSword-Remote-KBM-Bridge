#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <steam/isteamremoteplay.h>
#include <share.h>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <cstddef>
#include <cmath>
#include <climits>
#include "keybindings_v1_3_beta.h"

namespace {
constexpr std::uintptr_t kWorld = 0x08D67CE8;
constexpr std::uintptr_t kAppendName = 0x012C1CB0;
constexpr unsigned kProcessEventSlot = 0x4D;
constexpr int kFunctionCount = 38;
// Half Sword 0.6.15 FName comparison indices. Some hosts' saved InputSettings
// omit LMB or leave FKey's optional cached details empty; UPlayerInput::InputKey
// identifies these keys by FName.
constexpr int kLeftMouseName = 3146;
constexpr int kRightMouseName = 3155;
const wchar_t *const kNames[kFunctionCount] = {
    L"Key Forward_K2Node_InputActionEvent_24", L"Key Forward_K2Node_InputActionEvent_25",
    L"Key Back_K2Node_InputActionEvent_26", L"Key Back_K2Node_InputActionEvent_27",
    L"Key Left_K2Node_InputActionEvent_30", L"Key Left_K2Node_InputActionEvent_31",
    L"Key Right_K2Node_InputActionEvent_28", L"Key Right_K2Node_InputActionEvent_29",
    L"Turn Right / Left Mouse_K2Node_InputAxisEvent_16",
    L"Look Up / Down Mouse_K2Node_InputAxisEvent_17",
    L"Grab Left_K2Node_InputActionEvent_18", L"Grab Left_K2Node_InputActionEvent_19",
    L"Grab Right_K2Node_InputActionEvent_20", L"Grab Right_K2Node_InputActionEvent_21",
    L"Jump_K2Node_InputActionEvent_14", L"Jump_K2Node_InputActionEvent_15",
    L"Run_K2Node_InputActionEvent_4", L"Run_K2Node_InputActionEvent_5",
    L"Crouch Hold_K2Node_InputActionEvent_9", L"Crouch Hold_K2Node_InputActionEvent_10",
    L"Thrust_K2Node_InputActionEvent_0", L"Thrust_K2Node_InputActionEvent_1",
    L"Swap Hands_K2Node_InputActionEvent_16", L"Swap Hands_K2Node_InputActionEvent_17",
    L"RightMouseButton_K2Node_InputKeyEvent_0", L"RightMouseButton_K2Node_InputKeyEvent_1",
    L"Inventory_K2Node_InputActionEvent_33",
    L"PhotoMode_K2Node_InputActionEvent_32",
    L"Change Camera_K2Node_InputActionEvent_2",
    L"Toggle Camera Lock_K2Node_InputActionEvent_13",
    L"Talk_K2Node_InputActionEvent_22", L"Talk_K2Node_InputActionEvent_23",
    L"Right Arm Axis_K2Node_InputAxisEvent_2",
    L"Left Arm Axis_K2Node_InputAxisEvent_3",
    L"SloMo_K2Node_InputActionEvent_8",
    L"Pause_K2Node_InputActionEvent_3",
    L"InpActEvt_H_K2Node_InputKeyEvent_4",
    L"InpActEvt_MiddleMouseButton_K2Node_InputKeyEvent_2"
};
struct Array { void *data; int count; int capacity; };
struct String { wchar_t *data; int count; int capacity; };
struct Name { int comparison; unsigned number; };
struct Key { Name name; std::uintptr_t details; std::uintptr_t shared_controller; };
struct ActionMapping { Name action; unsigned char modifiers[8]; Key key; };
struct AxisMapping { Name axis; float scale; unsigned char pad[4]; Key key; };
static_assert(sizeof(AxisMapping) == 0x28);
struct Object;
struct ObjectItem { Object *object; unsigned char pad[16]; };
struct ObjectArray { ObjectItem **chunks; std::uint64_t pad; int max_elements, num_elements, max_chunks, num_chunks; };
struct InputKeyParams {
    Key key;
    int input_device = -1;
    int event = 0;
    int num_samples = 0;
    float delta_time = 1.0f / 60.0f;
    double delta_x = 0, delta_y = 0, delta_z = 0;
    bool gamepad_override = false;
};
static_assert(sizeof(Key) == 24 && offsetof(InputKeyParams, event) == 0x1C &&
    offsetof(InputKeyParams, delta_x) == 0x28 &&
    offsetof(InputKeyParams, gamepad_override) == 0x40);
struct Object { void *vtable; unsigned flags; int index; void *type; Name name; void *outer; };
using AppendName = void (*)(const Name *, String &);
using ProcessEvent = void (*)(void *, void *, void *);
using InputKey = bool (*)(void *, const InputKeyParams &);

HMODULE g_self;
HWND g_window;
UINT g_message;
FILE *g_log;
RemotePlayInput_t g_events[64];
uint32 g_count;
unsigned g_keys = 0, g_mouse = 0, g_buttons = 0, g_dropped = 0, g_repeat_suppressed = 0;
bool g_pressed[16]{};
void *g_direct_pawn = nullptr;
bool g_joiner_z_down = false, g_joiner_tab_down = false;
unsigned g_motion_merged = 0;
bool g_left_down = false, g_right_down = false;
bool g_space_down = false, g_space_using_native = false;
bool g_x_down = false, g_x_using_native = false;
Key g_left_key{}, g_right_key{}, g_space_key{}, g_x_key{};
Key g_mouse_x_key{}, g_mouse_y_key{};
Key g_movement_keys[4]{};
const wchar_t *const kMovementKeyNames[4] = {L"W", L"S", L"A", L"D"};
const char *const kMovementKeyLabels[4] = {"W", "S", "A", "D"};
unsigned g_native_mouse_reports = 0;
constexpr int kHeldKeyCount = 8;
const wchar_t *const kHeldKeyNames[kHeldKeyCount] = {
    L"Q", L"E", L"SpaceBar", L"LeftShift", L"LeftControl", L"LeftAlt", L"X", L"G"
};
const char *const kHeldKeyLabels[kHeldKeyCount] = {
    "Q", "E", "Space", "Shift from Caps", "Ctrl", "Alt", "X", "G"
};
Key g_held_keys[kHeldKeyCount]{};
bool g_held_down[kHeldKeyCount]{};
bool g_joiner_g_down = false, g_effective_g_down = false, g_g_using_native = false;
void *g_player_input = nullptr;
void *g_player_controller = nullptr;
void *g_player1_controller = nullptr;
void *g_player1_input = nullptr;
void *g_is_input_key_down = nullptr;
bool g_key_query_warned = false;
InputKey g_input_key = nullptr;
struct PendingRelease { Key key; const char *label; ULONGLONG due; unsigned attempts; bool active; };
PendingRelease g_pending[2 + kHeldKeyCount]{};
bool g_mouse_validation_reported = false;
bool g_native_keys_ready = false;
HANDLE g_settings_mapping;
BridgeSettings *g_settings;
LONG g_fallback_sensitivity = 250;
void *g_class = nullptr;
void *g_functions[kFunctionCount]{};
unsigned g_errors = 0;
unsigned g_full_input_batches = 0;
FILE *g_record_file = nullptr;
int g_record_player = 0;
HHOOK g_raw_record_hook = nullptr;
ULONGLONG g_native_retry_after = 0;
ULONGLONG g_native_failure_logged = 0;
const char *g_native_failure_stage = nullptr;
bool g_native_key_scan_attempted = false;

bool readable(const void *pointer, size_t bytes) {
    MEMORY_BASIC_INFORMATION info{};
    if (!pointer || !VirtualQuery(pointer, &info, sizeof(info))) return false;
    const auto start = reinterpret_cast<std::uintptr_t>(pointer);
    const auto end = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
    return info.State == MEM_COMMIT && !(info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
        start >= reinterpret_cast<std::uintptr_t>(info.BaseAddress) && bytes <= end - start;
}

bool executable(const void *pointer) {
    MEMORY_BASIC_INFORMATION info{};
    return pointer && VirtualQuery(pointer, &info, sizeof(info)) && info.State == MEM_COMMIT &&
        (info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
}

bool resolve_native_input() {
    const ULONGLONG now = GetTickCount64();
    if (g_native_retry_after && now < g_native_retry_after) return false;
    auto fail = [now](const char *stage) -> bool {
        g_native_retry_after = now + 2000;
        if (g_native_failure_stage != stage || now - g_native_failure_logged >= 10000) {
            std::fprintf(g_log, "Native input discovery failed at: %s; retry delayed 2000 ms\n", stage);
            std::fflush(g_log);
            g_native_failure_stage = stage;
            g_native_failure_logged = now;
        }
        return false;
    };
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    auto *world = reinterpret_cast<void **>(base + kWorld);
    if (!readable(world, 8) || !*world) return fail("world");
    auto *instance = reinterpret_cast<void **>(reinterpret_cast<std::uintptr_t>(*world) + 0x1D8);
    if (!readable(instance, 8) || !*instance) return fail("game instance");
    auto *players = reinterpret_cast<Array *>(reinterpret_cast<std::uintptr_t>(*instance) + 0x38);
    if (!readable(players, sizeof(Array)) || players->count != 2 || !readable(players->data, 16)) return fail("two local players");
    void *inputs[2]{}, *controllers[2]{};
    for (int i = 0; i < 2; ++i) {
        auto *local = static_cast<void **>(players->data)[i];
        auto *controller = reinterpret_cast<void **>(reinterpret_cast<std::uintptr_t>(local) + 0x30);
        if (!readable(controller, 8) || !*controller) return fail(i ? "Player 2 controller" : "Player 1 controller");
        controllers[i] = *controller;
        auto *input = reinterpret_cast<void **>(reinterpret_cast<std::uintptr_t>(*controller) + 0x408);
        if (!readable(input, 8) || !readable(*input, 0x320)) return fail(i ? "Player 2 input object" : "Player 1 input object");
        inputs[i] = *input;
    }
    if (inputs[0] == inputs[1]) return fail("independent player input objects");
    auto *vtable = *reinterpret_cast<void ***>(inputs[1]);
    if (!readable(vtable, 89 * 8) || *reinterpret_cast<void ***>(inputs[0]) != vtable) return fail("input virtual table");
    auto input_key = reinterpret_cast<InputKey>(vtable[88]);
    if (!executable(reinterpret_cast<void *>(input_key)) ||
        reinterpret_cast<std::uintptr_t>(input_key) != base + 0x05CF26E0) return fail("InputKey function address");
    if (!g_native_keys_ready) {
        // Walking the complete Unreal object registry can take hundreds of
        // milliseconds on some hosts. Never repeat that expensive pass during
        // the same bridge run. Failed optional/native keys use existing
        // blueprint fallbacks until the game and bridge are restarted.
        if (g_native_key_scan_attempted) return false;
        g_native_key_scan_attempted = true;
        auto *objects = reinterpret_cast<ObjectArray *>(base + 0x08BFD240);
        if (!readable(objects, sizeof(ObjectArray)) || objects->num_chunks < 1 ||
            objects->num_chunks > 128 || objects->num_elements < 1 ||
            objects->num_elements > 4000000 ||
            !readable(objects->chunks, objects->num_chunks * sizeof(void *))) return fail("Unreal object registry");
        auto append = reinterpret_cast<AppendName>(base + kAppendName);
        if (!executable(reinterpret_cast<void *>(append))) return fail("Unreal name reader");
        Key left{}, right{}, space{}, x_key{}, mouse_x{}, mouse_y{};
        Key held_keys[kHeldKeyCount]{};
        unsigned input_settings_candidates = 0;
        for (int i = 0; i < objects->num_elements; ++i) {
            auto *chunk = objects->chunks[i / 65536];
            if (!readable(chunk, sizeof(ObjectItem))) continue;
            auto *item = chunk + i % 65536;
            if (!readable(item, sizeof(ObjectItem)) || !readable(item->object, 0xB0)) continue;
            auto *object = item->object;
            auto *actions = reinterpret_cast<Array *>(reinterpret_cast<std::uintptr_t>(object) + 0x90);
            auto *axes = reinterpret_cast<Array *>(reinterpret_cast<std::uintptr_t>(object) + 0xA0);
            if (actions->count < 18 || actions->count > 100 || axes->count < 4 || axes->count > 80 ||
                actions->capacity < actions->count || actions->capacity > 128 ||
                !readable(actions->data, actions->count * sizeof(ActionMapping)) ||
                !readable(axes->data, axes->count * sizeof(AxisMapping))) continue;
            wchar_t object_name[128]{}; String shown{object_name, 0, 128};
            append(&object->name, shown);
            if (wcscmp(object_name, L"Default__InputSettings") != 0) continue;
            ++input_settings_candidates;
            Key candidate_left{}, candidate_right{}, candidate_space{}, candidate_x{},
                candidate_mouse_x{}, candidate_mouse_y{};
            Key candidate_held[kHeldKeyCount]{};
            for (int n = 0; n < actions->count; ++n) {
                auto *mapping = static_cast<ActionMapping *>(actions->data) + n;
                wchar_t key_name[128]{}; String key_text{key_name, 0, 128};
                append(&mapping->key.name, key_text);
                if (wcscmp(key_name, L"LeftMouseButton") == 0) candidate_left = mapping->key;
                if (wcscmp(key_name, L"RightMouseButton") == 0) candidate_right = mapping->key;
                if (wcscmp(key_name, L"SpaceBar") == 0) candidate_space = mapping->key;
                if (wcscmp(key_name, L"X") == 0) candidate_x = mapping->key;
                for (int key_index = 0; key_index < kHeldKeyCount; ++key_index)
                    if (wcscmp(key_name, kHeldKeyNames[key_index]) == 0)
                        candidate_held[key_index] = mapping->key;
            }
            for (int n = 0; n < axes->count; ++n) {
                auto *mapping = static_cast<AxisMapping *>(axes->data) + n;
                wchar_t key_name[128]{}; String key_text{key_name, 0, 128};
                append(&mapping->key.name, key_text);
                if (wcscmp(key_name, L"MouseX") == 0) candidate_mouse_x = mapping->key;
                if (wcscmp(key_name, L"MouseY") == 0) candidate_mouse_y = mapping->key;
                for (int movement = 0; movement < 4; ++movement)
                    if (wcscmp(key_name, kMovementKeyNames[movement]) == 0)
                        g_movement_keys[movement] = mapping->key;
            }
            if (!candidate_left.name.comparison) candidate_left.name = {kLeftMouseName, 0};
            if (!candidate_right.name.comparison) candidate_right.name = {kRightMouseName, 0};
            const bool candidate_valid = candidate_left.name.comparison == kLeftMouseName &&
                candidate_right.name.comparison == kRightMouseName;
            std::fprintf(g_log,
                "InputSettings candidate %u: actions=%d axes=%d LMB=%d/%d RMB=%d/%d usable=%d\n",
                input_settings_candidates, actions->count, axes->count,
                candidate_left.name.comparison, readable(reinterpret_cast<void *>(candidate_left.details), 8) ? 1 : 0,
                candidate_right.name.comparison, readable(reinterpret_cast<void *>(candidate_right.details), 8) ? 1 : 0,
                candidate_valid ? 1 : 0);
            std::fflush(g_log);
            if (!candidate_valid) continue;
            left = candidate_left; right = candidate_right; space = candidate_space; x_key = candidate_x;
            mouse_x = candidate_mouse_x; mouse_y = candidate_mouse_y;
            for (int key_index = 0; key_index < kHeldKeyCount; ++key_index)
                held_keys[key_index] = candidate_held[key_index];
            break;
        }
        if (left.name.comparison != kLeftMouseName || right.name.comparison != kRightMouseName)
            return fail("essential mouse key mappings");
        for (int key_index = 0; key_index < kHeldKeyCount; ++key_index) {
            if (!held_keys[key_index].name.comparison) {
                held_keys[key_index] = {};
                std::fprintf(g_log, "Optional native key unavailable: %s; its bridge action will use fallback behavior\n",
                    kHeldKeyLabels[key_index]);
            }
        }
        g_left_key = left; g_right_key = right; g_space_key = space; g_x_key = x_key;
        if (mouse_x.name.comparison && mouse_y.name.comparison) {
            g_mouse_x_key = mouse_x; g_mouse_y_key = mouse_y;
        }
        for (int key_index = 0; key_index < kHeldKeyCount; ++key_index)
            g_held_keys[key_index] = held_keys[key_index];
        g_native_keys_ready = true;
        std::fprintf(g_log, "Game InputSettings keys: LMB=%d RMB=%d Space=%d X=%d MouseX=%d MouseY=%d\n",
            left.name.comparison, right.name.comparison, space.name.comparison, x_key.name.comparison,
            g_mouse_x_key.name.comparison, g_mouse_y_key.name.comparison);
        std::fflush(g_log);
    }
    if (g_player_input != inputs[1]) {
        g_player_input = inputs[1];
        g_player_controller = controllers[1];
        g_left_down = g_right_down = g_space_down = g_x_down = false;
        g_effective_g_down = g_g_using_native = false;
        for (bool &held : g_held_down) held = false;
        for (auto &pending : g_pending) pending.active = false;
        std::fprintf(g_log, "Player 2 input object ready: %p\n", g_player_input);
        std::fflush(g_log);
    }
    g_player_controller = controllers[1];
    g_player1_controller = controllers[0];
    g_player1_input = inputs[0];
    g_input_key = input_key;
    g_native_retry_after = 0;
    g_native_failure_stage = nullptr;
    return true;
}

#include "lockon_core_v1_3_beta.h"

bool resolve_key_state_query() {
    if (g_is_input_key_down) return true;
    if (!g_player_controller || !readable(g_player_controller, sizeof(Object))) return false;
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    auto append = reinterpret_cast<AppendName>(base + kAppendName);
    if (!executable(reinterpret_cast<void *>(append))) return false;
    void *klass = static_cast<Object *>(g_player_controller)->type;
    for (unsigned depth = 0; klass && depth < 20; ++depth) {
        auto *children = reinterpret_cast<void **>(reinterpret_cast<std::uintptr_t>(klass) + 0x48);
        if (!readable(children, 8)) break;
        void *child = *children;
        for (unsigned visited = 0; child && visited < 2000; ++visited) {
            if (!readable(child, sizeof(Object))) break;
            wchar_t name[128]{}; String shown{name, 0, 128};
            append(&static_cast<Object *>(child)->name, shown);
            if (wcscmp(name, L"IsInputKeyDown") == 0) {
                g_is_input_key_down = child;
                std::fprintf(g_log, "Player 2 IsInputKeyDown query available\n");
                std::fflush(g_log);
                return true;
            }
            auto *next = reinterpret_cast<void **>(reinterpret_cast<std::uintptr_t>(child) + 0x28);
            if (!readable(next, 8)) break;
            child = *next;
        }
        auto *super = reinterpret_cast<void **>(reinterpret_cast<std::uintptr_t>(klass) + 0x40);
        if (!readable(super, 8)) break;
        klass = *super;
    }
    if (!g_key_query_warned) {
        std::fprintf(g_log, "Player 2 key-state query unavailable; release retries disabled\n");
        std::fflush(g_log);
        g_key_query_warned = true;
    }
    return false;
}

bool player2_key_down(const Key &key, bool &down) {
    if (!resolve_key_state_query()) return false;
    auto *vtable = *reinterpret_cast<void ***>(g_player_controller);
    if (!readable(vtable, (kProcessEventSlot + 1) * sizeof(void *))) return false;
    auto process_event = reinterpret_cast<ProcessEvent>(vtable[kProcessEventSlot]);
    if (!executable(reinterpret_cast<void *>(process_event))) return false;
    struct Query { Key key; bool down; unsigned char padding[7]; } query{key, false, {}};
    static_assert(sizeof(Query) == 0x20 && offsetof(Query, down) == 0x18);
    process_event(g_player_controller, g_is_input_key_down, &query);
    down = query.down;
    return true;
}

bool player_key_down(int player, const Key &key, bool &down) {
    if (!resolve_key_state_query()) return false;
    void *controller = player == 1 ? g_player1_controller : g_player_controller;
    if (!controller || !readable(controller, sizeof(Object))) return false;
    auto *vtable = *reinterpret_cast<void ***>(controller);
    if (!readable(vtable, (kProcessEventSlot + 1) * sizeof(void *))) return false;
    auto process_event = reinterpret_cast<ProcessEvent>(vtable[kProcessEventSlot]);
    if (!executable(reinterpret_cast<void *>(process_event))) return false;
    struct Query { Key key; bool down; unsigned char padding[7]; } query{key, false, {}};
    process_event(controller, g_is_input_key_down, &query);
    down = query.down;
    return true;
}

void repair_player1_alt_after_focus() {
    static ULONGLONG focused_since = 0;
    static bool checked = false;
    static ULONGLONG verify_due = 0;
    const ULONGLONG now = GetTickCount64();
    if (GetForegroundWindow() != g_window) {
        focused_since = 0;
        checked = false;
        verify_due = 0;
        return;
    }
    if (!focused_since) focused_since = now;
    if (verify_due && now >= verify_due) {
        bool after = false;
        if (player_key_down(1, g_held_keys[5], after)) {
            std::fprintf(g_log, "P1 Alt focus release verified t=%llu game_after=%d\n",
                static_cast<unsigned long long>(now), after?1:0);
            if (g_record_file) std::fprintf(g_record_file,
                "P1-ALT-VERIFY t=%llu game_after=%d\n",
                static_cast<unsigned long long>(now), after?1:0);
            std::fflush(g_log);
            verify_due = 0;
        }
    }
    if (checked || now - focused_since < 150) return;
    // Wait for a real Alt release; never override an intentional held thrust.
    const bool physical_alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0 ||
        (GetAsyncKeyState(VK_LMENU) & 0x8000) != 0 ||
        (GetAsyncKeyState(VK_RMENU) & 0x8000) != 0;
    if (physical_alt || !resolve_native_input() || !g_player1_input ||
        !g_held_keys[5].name.comparison || !g_input_key) return;
    bool game_alt = false;
    if (!player_key_down(1, g_held_keys[5], game_alt)) return;
    checked = true;
    bool handled = false, after = game_alt;
    if (game_alt) {
        InputKeyParams release{};
        release.key = g_held_keys[5];
        release.event = 1;
        handled = g_input_key(g_player1_input, release);
        player_key_down(1, g_held_keys[5], after);
        verify_due = now + 100;
    }
    std::fprintf(g_log, "P1 Alt focus check t=%llu physical=0 game_before=%d release=%d game_after=%d\n",
        static_cast<unsigned long long>(now), game_alt?1:0, handled?1:0, after?1:0);
    if (g_record_file) std::fprintf(g_record_file,
        "P1-ALT-FOCUS t=%llu physical=0 game_before=%d release=%d game_after=%d\n",
        static_cast<unsigned long long>(now), game_alt?1:0, handled?1:0, after?1:0);
    std::fflush(g_log);
}

void schedule_release(int index, const Key &key, const char *label, bool down) {
    if (index < 0 || index >= 2 + kHeldKeyCount) return;
    auto &pending = g_pending[index];
    if (down) { pending.active = false; return; }
    pending = {key, label, GetTickCount64() + 50, 0, true};
}

void verify_releases() {
    const ULONGLONG now = GetTickCount64();
    bool due = false;
    for (const auto &pending : g_pending)
        if (pending.active && now >= pending.due) due = true;
    if (!due || !resolve_native_input()) return;
    for (auto &pending : g_pending) {
        if (!pending.active || now < pending.due) continue;
        bool still_down = false;
        if (!player2_key_down(pending.key, still_down)) {
            pending.active = false;
            continue;
        }
        if (!still_down) { pending.active = false; continue; }
        if (pending.attempts >= 5) {
            std::fprintf(g_log, "%s remained down after five verified release retries\n", pending.label);
            std::fflush(g_log);
            pending.active = false;
            continue;
        }
        InputKeyParams params{};
        params.key = pending.key;
        params.event = 1;
        const bool handled = g_input_key(g_player_input, params);
        ++pending.attempts;
        // Give the game time to consume the release after a delayed input batch.
        pending.due = now + (pending.attempts <= 2 ? 75 : 250);
        std::fprintf(g_log, "t=%llu %s was still down in Player 2; release retry %u handled=%d\n",
            static_cast<unsigned long long>(GetTickCount64()),
            pending.label, pending.attempts, handled ? 1 : 0);
        std::fflush(g_log);
    }
}

void mouse_button(const Key &key, bool down, bool &held, const char *label) {
    if (!resolve_native_input()) {
        if (!g_mouse_validation_reported) {
            std::fprintf(g_log, "P2 mouse input unavailable: click both mouse buttons as P1 and check game version\n");
            std::fflush(g_log); g_mouse_validation_reported = true;
        }
        ++g_dropped;
        return;
    }
    const int pending_index = &held == &g_left_down ? 0 : 1;
    if (held == down) {
        schedule_release(pending_index, key, label, down);
        return;
    }
    InputKeyParams params{};
    params.key = (&key == &g_left_key) ? g_left_key : g_right_key;
    params.event = down ? 0 : 1;
    params.delta_x = down ? 1.0 : 0.0;
    const bool handled = g_input_key(g_player_input, params);
    held = down;
    schedule_release(pending_index, params.key, label, down);
    std::fprintf(g_log, "t=%llu %s %s: handled=%d\n",
        static_cast<unsigned long long>(GetTickCount64()), label,
        down ? "down" : "up", handled ? 1 : 0);
    std::fflush(g_log);
    ++g_buttons;
}

bool space_button(bool down) {
    if (!resolve_native_input() || !g_space_key.name.comparison) return false;
    if (g_space_down == down) return true;
    InputKeyParams params{};
    params.key = g_space_key;
    params.event = down ? 0 : 1;
    params.delta_x = down ? 1.0 : 0.0;
    bool handled = g_input_key(g_player_input, params);
    g_space_down = down;
    std::fprintf(g_log, "Space %s via native input: handled=%d\n", down ? "down" : "up", handled ? 1 : 0);
    std::fflush(g_log);
    return true;
}

bool x_button(bool down) {
    if (!resolve_native_input() || !g_x_key.name.comparison) return false;
    if (g_x_down == down) return true;
    InputKeyParams params{};
    params.key = g_x_key;
    params.event = down ? 0 : 1;
    params.delta_x = down ? 1.0 : 0.0;
    const bool handled = g_input_key(g_player_input, params);
    g_x_down = down;
    std::fprintf(g_log, "X %s via native input: handled=%d\n", down ? "down" : "up", handled ? 1 : 0);
    std::fflush(g_log);
    return true;
}

bool held_key_button(int index, bool down) {
    if (index < 0 || index >= kHeldKeyCount || !resolve_native_input() ||
        !g_held_keys[index].name.comparison) return false;
    if (g_held_down[index] == down) {
        schedule_release(2 + index, g_held_keys[index], kHeldKeyLabels[index], down);
        return true;
    }
    InputKeyParams params{};
    params.key = g_held_keys[index];
    params.event = down ? 0 : 1;
    params.delta_x = down ? 1.0 : 0.0;
    const bool handled = g_input_key(g_player_input, params);
    g_held_down[index] = down;
    schedule_release(2 + index, g_held_keys[index], kHeldKeyLabels[index], down);
    std::fprintf(g_log, "t=%llu %s %s via native input: handled=%d\n",
        static_cast<unsigned long long>(GetTickCount64()), kHeldKeyLabels[index],
        down ? "down" : "up", handled ? 1 : 0);
    std::fflush(g_log);
    return true;
}

void native_mouse_axis(const Key &key, float delta, const char *label) {
    if (!delta || !resolve_native_input() || !key.name.comparison) return;
    InputKeyParams params{};
    params.key = key;
    params.event = 4; // IE_Axis
    params.num_samples = 1;
    params.delta_x = delta;
    const bool handled = g_input_key(g_player_input, params);
    if (g_native_mouse_reports++ < 12) {
        std::fprintf(g_log, "Native %s axis %.3f: handled=%d\n", label, delta, handled ? 1 : 0);
        std::fflush(g_log);
    }
}

bool resolve_player(void *&pawn, ProcessEvent &process_event) {
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    auto *world_slot = reinterpret_cast<void **>(base + kWorld);
    if (!readable(world_slot, sizeof(void *))) return false;
    auto *instance_slot = reinterpret_cast<void **>(reinterpret_cast<std::uintptr_t>(*world_slot) + 0x1D8);
    if (!readable(instance_slot, sizeof(void *))) return false;
    auto *players = reinterpret_cast<Array *>(reinterpret_cast<std::uintptr_t>(*instance_slot) + 0x38);
    if (!readable(players, sizeof(Array)) || players->count < 2 || players->count > 8 ||
        !readable(players->data, sizeof(void *) * players->count)) return false;
    void *local = static_cast<void **>(players->data)[1];
    auto *controller_slot = reinterpret_cast<void **>(reinterpret_cast<std::uintptr_t>(local) + 0x30);
    if (!readable(controller_slot, sizeof(void *))) return false;
    auto *pawn_slot = reinterpret_cast<void **>(reinterpret_cast<std::uintptr_t>(*controller_slot) + 0x2D0);
    if (!readable(pawn_slot, sizeof(void *)) || !readable(*pawn_slot, sizeof(Object))) return false;
    pawn = *pawn_slot;
    auto *object = static_cast<Object *>(pawn);
    auto *vtable = static_cast<void **>(object->vtable);
    if (!readable(vtable, sizeof(void *) * (kProcessEventSlot + 1))) return false;
    process_event = reinterpret_cast<ProcessEvent>(vtable[kProcessEventSlot]);
    if (!executable(reinterpret_cast<void *>(process_event))) return false;
    if (g_class == object->type) return true;

    g_class = nullptr;
    for (auto &function : g_functions) function = nullptr;
    auto *child_slot = reinterpret_cast<void **>(reinterpret_cast<std::uintptr_t>(object->type) + 0x48);
    auto append = reinterpret_cast<AppendName>(base + kAppendName);
    if (!readable(child_slot, sizeof(void *)) || !executable(reinterpret_cast<void *>(append))) return false;
    unsigned visited = 0;
    for (void *child = *child_slot; child && visited++ < 2000;) {
        if (!readable(child, sizeof(Object) + sizeof(void *))) break;
        wchar_t buffer[1024]{};
        String name{buffer, 0, 1024};
        append(&static_cast<Object *>(child)->name, name);
        for (int i = 0; i < kFunctionCount; ++i)
            if (wcsstr(buffer, kNames[i])) g_functions[i] = child;
        child = *reinterpret_cast<void **>(reinterpret_cast<std::uintptr_t>(child) + 0x28);
    }
    unsigned found = 0;
    for (const auto function : g_functions) if (function) ++found;
    std::fprintf(g_log, "Player 2 input functions available: %u/%d\n", found, kFunctionCount);
    std::fflush(g_log);
    for (int i = 0; i < 34; ++i) if (!g_functions[i]) return false;
    g_class = object->type;
    return true;
}

void perform(void *pawn, ProcessEvent process_event, int function, float axis = 0) {
    if (function < 0 || function >= kFunctionCount || !g_functions[function]) { ++g_dropped; return; }
    if (function == 8 || function == 9 || function == 32 || function == 33)
        process_event(pawn, g_functions[function], &axis);
    else {
        unsigned char key_parameter[0x18]{};
        process_event(pawn, g_functions[function], key_parameter);
    }
}

void sync_host_g(void *pawn, ProcessEvent process_event) {
    const bool mirror = g_settings &&
        InterlockedCompareExchange(&g_settings->host_g_mirrors_player2, 0, 0) != 0;
    const bool host_down = mirror && (GetAsyncKeyState('G') & 0x8000) != 0;
    const bool effective = host_down || g_joiner_g_down;
    if (effective == g_effective_g_down) return;
    if (effective) {
        g_g_using_native = held_key_button(7, true);
        if (!g_g_using_native) {
            perform(pawn, process_event, 30);
            g_pressed[15] = true;
        }
    } else if (g_g_using_native) {
        if (!held_key_button(7, false)) {
            std::fprintf(g_log, "G native release unavailable; retry on next input batch\n");
            std::fflush(g_log);
            return;
        }
        g_g_using_native = false;
    } else {
        perform(pawn, process_event, 31);
        g_pressed[15] = false;
    }
    g_effective_g_down = effective;
    std::fprintf(g_log, "G Player 2 %s source host=%d joiner=%d mirror=%d\n",
        effective ? "down" : "up", host_down ? 1 : 0, g_joiner_g_down ? 1 : 0, mirror ? 1 : 0);
    std::fflush(g_log);
}

void handle_batch() {
    void *pawn = nullptr;
    ProcessEvent process_event = nullptr;
    if (!resolve_player(pawn, process_event)) { g_dropped += g_count; return; }
    if (pawn != g_direct_pawn) {
        // Direct action states belong to a pawn, which is replaced between rounds.
        // A later key repeat may press the new pawn, but an old held state must
        // never suppress that press or be carried into a new fighter.
        if (g_direct_pawn) {
            std::fprintf(g_log, "Player 2 pawn changed %p -> %p; direct input states cleared\n", g_direct_pawn, pawn);
            if (g_record_file) std::fprintf(g_record_file,
                "PAWN-CHANGE t=%llu old=%p new=%p direct-states-cleared=1\n",
                static_cast<unsigned long long>(GetTickCount64()), g_direct_pawn, pawn);
        }
        for (bool &pressed : g_pressed) pressed = false;
        g_direct_pawn = pawn;
    }
    sync_host_g(pawn, process_event);
    const LONG sensitivity = g_settings
        ? InterlockedCompareExchange(&g_settings->sensitivity_per_thousand, 0, 0)
        : g_fallback_sensitivity;
    const float mouse_scale = static_cast<float>(sensitivity >= 10 && sensitivity <= 2000
        ? sensitivity : 250) / 1000.0f;
    for (uint32 i = 0; i < g_count; ++i) {
        RemotePlayInput_t event = g_events[i];
        if (g_record_file) {
            const auto stamp = static_cast<unsigned long long>(GetTickCount64());
            if (event.m_eType == k_ERemotePlayInputKeyDown || event.m_eType == k_ERemotePlayInputKeyUp)
                std::fprintf(g_record_file, "RAW t=%llu type=%s scancode=%d\n", stamp,
                    event.m_eType == k_ERemotePlayInputKeyDown ? "key-down" : "key-up",
                    static_cast<int>(event.m_Key.m_eScancode));
            else if (event.m_eType == k_ERemotePlayInputMouseButtonDown || event.m_eType == k_ERemotePlayInputMouseButtonUp)
                std::fprintf(g_record_file, "RAW t=%llu type=%s mouse-button=%d\n", stamp,
                    event.m_eType == k_ERemotePlayInputMouseButtonDown ? "mouse-down" : "mouse-up",
                    static_cast<int>(event.m_eMouseButton));
            else if (event.m_eType == k_ERemotePlayInputMouseMotion)
                std::fprintf(g_record_file, "RAW t=%llu type=mouse-motion dx=%d dy=%d\n", stamp,
                    event.m_MouseMotion.m_nDeltaX, event.m_MouseMotion.m_nDeltaY);
            else std::fprintf(g_record_file, "RAW t=%llu type=%d\n", stamp, static_cast<int>(event.m_eType));
        }
        if (event.m_eType == k_ERemotePlayInputKeyDown || event.m_eType == k_ERemotePlayInputKeyUp ||
            event.m_eType == k_ERemotePlayInputMouseButtonDown || event.m_eType == k_ERemotePlayInputMouseButtonUp) {
            const bool was_key = event.m_eType == k_ERemotePlayInputKeyDown || event.m_eType == k_ERemotePlayInputKeyUp;
            const bool was_down = event.m_eType == k_ERemotePlayInputKeyDown ||
                event.m_eType == k_ERemotePlayInputMouseButtonDown;
            LONG token = was_key ? static_cast<LONG>(event.m_Key.m_eScancode)
                : kMouseBindingBase + static_cast<LONG>(event.m_eMouseButton);
            if (token == 225 || token == 229) { ++g_dropped; continue; } // Steam Remote Play reserves Shift.
            if (token == 228) token = 224; // right Ctrl follows left Ctrl
            if (token == 230) token = 226; // right Alt follows left Alt
            int binding = -1;
            for (int slot = 0; slot < kBindingCount; ++slot) {
                const LONG configured = g_settings
                    ? InterlockedCompareExchange(&g_settings->bindings[slot], 0, 0)
                    : kDefaultBindings[slot];
                if (configured == token) { binding = slot; break; }
            }
            if (binding < 0) {
                if (g_record_file) std::fprintf(g_record_file, "RAW-MAP t=%llu received=%ld result=unmatched\n",
                    static_cast<unsigned long long>(GetTickCount64()), token);
                ++g_dropped; continue;
            }
            const int canonical = kCanonicalBindings[binding];
            if (g_record_file) std::fprintf(g_record_file,
                "RAW-MAP t=%llu received=%ld binding-slot=%d canonical=%d\n",
                static_cast<unsigned long long>(GetTickCount64()), token, binding, canonical);
            if (canonical >= kMouseBindingBase) {
                event.m_eType = was_down ? k_ERemotePlayInputMouseButtonDown : k_ERemotePlayInputMouseButtonUp;
                event.m_eMouseButton = static_cast<ERemotePlayMouseButton>(canonical - kMouseBindingBase);
            } else {
                event.m_eType = was_down ? k_ERemotePlayInputKeyDown : k_ERemotePlayInputKeyUp;
                event.m_Key.m_eScancode = canonical;
            }
        }
        if (event.m_eType == k_ERemotePlayInputMouseMotion) {
            const int x = event.m_MouseMotion.m_nDeltaX;
            const int y = event.m_MouseMotion.m_nDeltaY;
            const bool native_alt_mouse = g_settings &&
                InterlockedCompareExchange(&g_settings->alt_native_mouse, 0, 0) &&
                g_held_down[5] && g_mouse_x_key.name.comparison && g_mouse_y_key.name.comparison;
            if (native_alt_mouse) {
                constexpr float kAltThrustMouseBoost=1.20f;
                if (x) native_mouse_axis(g_mouse_x_key, static_cast<float>(x) * mouse_scale * kAltThrustMouseBoost, "MouseX");
                if (y) native_mouse_axis(g_mouse_y_key, -static_cast<float>(y) * mouse_scale * kAltThrustMouseBoost, "MouseY");
            } else {
                if (x) perform(pawn, process_event, 8, static_cast<float>(x) * mouse_scale);
                if (y) perform(pawn, process_event, 9, static_cast<float>(y) * mouse_scale);
            }
            ++g_mouse;
        } else if (event.m_eType == k_ERemotePlayInputKeyDown ||
                   event.m_eType == k_ERemotePlayInputKeyUp) {
            const int phase = event.m_eType == k_ERemotePlayInputKeyUp ? 1 : 0;
            int held_index = -1;
            switch (event.m_Key.m_eScancode) {
                case 20: held_index = 0; break; // Q
                case 8: held_index = 1; break;  // E
                case 44: held_index = 2; break; // Space
                case 57: held_index = 3; break; // Caps Lock becomes LeftShift
                case 224: case 228: held_index = 4; break; // Ctrl
                case 226: case 230: held_index = 5; break; // Alt
                case 27: held_index = 6; break; // X
                case 10: held_index = 7; break; // G
            }
            if (held_index >= 0) {
                if (held_index == 7) {
                    g_joiner_g_down = phase == 0;
                    sync_host_g(pawn, process_event);
                    ++g_keys;
                    continue;
                }
                const bool handled = held_key_button(held_index, phase == 0);
                static constexpr int fallback_functions[kHeldKeyCount] = {
                    10, 12, 14, 16, 18, 20, 22, 30
                };
                bool dispatched = handled;
                if (!handled) {
                    const int fallback = fallback_functions[held_index] + phase;
                    const int state_index = fallback / 2;
                    if (g_pressed[state_index] != (phase == 0)) {
                        perform(pawn, process_event, fallback);
                        g_pressed[state_index] = phase == 0;
                        dispatched = true;
                    } else ++g_repeat_suppressed;
                }
                if (g_record_file && dispatched) std::fprintf(g_record_file,
                    "RAW-DISPATCH t=%llu route=%s action=%s phase=%s\n",
                    static_cast<unsigned long long>(GetTickCount64()),
                    handled ? "native" : "direct-fallback", kHeldKeyLabels[held_index],
                    phase == 0 ? "down" : "up");
                ++g_keys;
                continue;
            }
            int function = -1;
            switch (event.m_Key.m_eScancode) {
                case 26: function = phase; break;       // W
                case 22: function = 2 + phase; break;   // S
                case 4: function = 4 + phase; break;    // A
                case 7: function = 6 + phase; break;    // D
                case 20: function = 10 + phase; break;  // Q: left grab
                case 8: function = 12 + phase; break;   // E: right grab
                case 44: // Space: native input preserves the game's press/hold/release state
                    if (!phase) {
                        g_space_using_native = space_button(true);
                        if (!g_space_using_native) function = 14;
                    } else if (g_space_using_native) {
                        space_button(false);
                        g_space_using_native = false;
                    } else function = 15;
                    if (function < 0) ++g_keys;
                    break;
                case 57: function = 16 + phase; break; // Caps Lock: run for joiner
                case 224: case 228: function = 18 + phase; break; // Ctrl: crouch
                case 226: case 230: function = 20 + phase; break; // Alt: thrust
                case 27: // Let the game interpret X tap versus hold and its release.
                    if (!phase) {
                        g_x_using_native = x_button(true);
                        if (!g_x_using_native) {
                            std::fprintf(g_log, "X native input unavailable; X ignored\n");
                            std::fflush(g_log);
                        }
                    } else if (g_x_using_native) {
                        x_button(false);
                        g_x_using_native = false;
                    }
                    if (function < 0) ++g_keys;
                    break;
                case 10: function = 30 + phase; break;  // G: taunt / hold to give up
                case 9: if (!phase) function = 34; break; // F: slow motion
                case 21: if (!phase) function = 26; break; // R: inventory
                case 6: break; // C: intentionally blocked for remote Player 2; photo mode cannot be closed there
                case 25: if (!phase) function = 28; break; // V: camera
                case 29: // Z: independently toggle Player 2 custom lock-on
                    if (phase) g_joiner_z_down = false;
                    else if (!g_joiner_z_down) {
                        g_joiner_z_down = true;
                        lockon13beta::toggle(1);
                        if (g_settings) InterlockedExchange(&g_settings->lockon_player2,
                            lockon13beta::enabled[1] ? 1 : 0);
                    } else ++g_repeat_suppressed;
                    ++g_keys;
                    continue;
                case 43: // Tab: cycle Player 2's current round-local target list
                    if (phase) g_joiner_tab_down = false;
                    else if (!g_joiner_tab_down) {
                        g_joiner_tab_down = true;
                        lockon13beta::cycle(1);
                    } else ++g_repeat_suppressed;
                    ++g_keys;
                    continue;
                case 41: case 19: if (!phase) function = 35; break; // Escape/P: pause
                case 11: if (!phase) function = 36; break; // H: undress
            }
            if (function >= 0) {
                const bool stateful = function < 26 || (function >= 30 && function < 32);
                if (!stateful || g_pressed[function / 2] != (phase == 0)) {
                    perform(pawn, process_event, function);
                    if (stateful) g_pressed[function / 2] = phase == 0;
                    if (g_record_file) std::fprintf(g_record_file,
                        "RAW-DISPATCH t=%llu route=direct function=%d phase=%s\n",
                        static_cast<unsigned long long>(GetTickCount64()), function,
                        phase == 0 ? "down" : "up");
                } else ++g_repeat_suppressed;
                ++g_keys;
            }
            else ++g_dropped;
        } else if (event.m_eType == k_ERemotePlayInputMouseButtonDown ||
                   event.m_eType == k_ERemotePlayInputMouseButtonUp) {
            const int phase = event.m_eType == k_ERemotePlayInputMouseButtonUp ? 1 : 0;
            if (event.m_eMouseButton == k_ERemotePlayMouseButtonLeft) {
                mouse_button(g_left_key, phase == 0, g_left_down, "LMB -> right hand");
                lockon13beta::set_arm(1, g_left_down || g_right_down);
                if (g_record_file) std::fprintf(g_record_file,
                    "RAW-DISPATCH t=%llu route=native action=right-hand phase=%s available=%d\n",
                    static_cast<unsigned long long>(GetTickCount64()), phase==0?"down":"up",
                    g_native_keys_ready?1:0);
            } else if (event.m_eMouseButton == k_ERemotePlayMouseButtonRight) {
                mouse_button(g_right_key, phase == 0, g_right_down, "RMB -> left hand");
                lockon13beta::set_arm(1, g_left_down || g_right_down);
                if (g_record_file) std::fprintf(g_record_file,
                    "RAW-DISPATCH t=%llu route=native action=left-hand phase=%s available=%d\n",
                    static_cast<unsigned long long>(GetTickCount64()), phase==0?"down":"up",
                    g_native_keys_ready?1:0);
            } else if (event.m_eMouseButton == k_ERemotePlayMouseButtonMiddle) {
                if (!phase) perform(pawn, process_event, 37); // game's MMB action
                ++g_buttons;
            } else ++g_dropped;
        } else ++g_dropped;
    }
    sync_host_g(pawn, process_event);
    if (g_record_file) std::fflush(g_record_file);
    verify_releases();
}

void release_held_keys() {
    void *pawn = nullptr;
    ProcessEvent process_event = nullptr;
    const bool have_pawn = resolve_player(pawn, process_event);
    for (int i = 0; i < 16; ++i) {
        if (g_pressed[i] && have_pawn) perform(pawn, process_event, i * 2 + 1);
        g_pressed[i] = false;
    }
    if (g_left_down) mouse_button(g_left_key, false, g_left_down, "LMB -> right hand");
    if (g_right_down) mouse_button(g_right_key, false, g_right_down, "RMB -> left hand");
    lockon13beta::set_arm(1, false);
    if (g_space_down) space_button(false);
    g_space_using_native = false;
    if (g_x_down) x_button(false);
    g_x_using_native = false;
    for (int index = 0; index < kHeldKeyCount; ++index)
        if (g_held_down[index]) held_key_button(index, false);
    g_left_down = g_right_down = g_space_down = g_x_down = false;
    g_joiner_g_down = g_effective_g_down = g_g_using_native = false;
    g_joiner_z_down = g_joiner_tab_down = false;
    g_direct_pawn = nullptr;
    for (bool &held : g_held_down) held = false;
}

void log_input_snapshot(int player = 2, FILE *output = nullptr) {
    if (!output) output = g_log;
    void *pawn = nullptr;
    ProcessEvent process_event = nullptr;
    bool pawn_ready = false;
    if (player == 2) pawn_ready = resolve_player(pawn, process_event);
    const bool input_ready = resolve_native_input();
    if (player == 1 && input_ready && g_player1_controller) {
        auto *slot = reinterpret_cast<void **>(reinterpret_cast<std::uintptr_t>(g_player1_controller) + 0x2D0);
        if (readable(slot, sizeof(void *)) && readable(*slot, sizeof(Object))) {
            pawn = *slot;
            pawn_ready = true;
        }
    }
    std::fprintf(output, "=== PLAYER %d INPUT SNAPSHOT t=%llu pawn=%d native=%d ===\n",
        player, static_cast<unsigned long long>(GetTickCount64()), pawn_ready ? 1 : 0, input_ready ? 1 : 0);
    auto report = [player, output](const char *label, const Key &key, bool bridge_down) {
        bool game_down = false;
        const bool known = player_key_down(player, key, game_down);
        std::fprintf(output, "%s bridge=%s game=%s\n", label,
            player == 2 ? (bridge_down ? "down" : "up") : "n/a",
            known ? (game_down ? "down" : "up") : "unknown");
    };
    if (input_ready) {
        report("Left mouse", g_left_key, g_left_down);
        report("Right mouse", g_right_key, g_right_down);
        for (int index = 0; index < kHeldKeyCount; ++index)
            report(kHeldKeyLabels[index], g_held_keys[index], g_held_down[index]);
        for (int movement = 0; movement < 4; ++movement)
            if (g_movement_keys[movement].name.comparison)
                report(kMovementKeyLabels[movement], g_movement_keys[movement],
                    player == 2 ? g_pressed[movement] : false);
    }
    if (player == 1) std::fprintf(output, "Physical P1 Alt=%d LMB=%d RMB=%d\n",
        (GetAsyncKeyState(VK_MENU) & 0x8000) != 0 ? 1 : 0,
        (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 ? 1 : 0,
        (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0 ? 1 : 0);
    if (player == 2) std::fprintf(output, "Movement bridge W=%d S=%d A=%d D=%d\n",
        g_pressed[0] ? 1 : 0, g_pressed[1] ? 1 : 0,
        g_pressed[2] ? 1 : 0, g_pressed[3] ? 1 : 0);
    if (pawn_ready) {
        auto flag = [pawn](std::uintptr_t offset) -> int {
            auto *value = reinterpret_cast<unsigned char *>(reinterpret_cast<std::uintptr_t>(pawn) + offset);
            return readable(value, 1) ? (*value ? 1 : 0) : -1;
        };
        std::fprintf(output, "Pawn R_Grab=%d L_Grab=%d R_Guard=%d L_Guard=%d Alt_Thrust=%d Alt_Key=%d\n",
            flag(0x2101), flag(0x2260), flag(0x1248), flag(0x1260),
            flag(0x1BF1), flag(0x4469));
    }
    lockon13beta::diagnostic_orientation(player - 1, output);
    std::fprintf(output, "=== END INPUT SNAPSHOT ===\n");
    std::fflush(output);
}

void log_issue_note(FILE *output) {
    if (!output || !g_settings) return;
    wchar_t note[128]{};
    for (int i = 0; i < 127; ++i) note[i] = g_settings->issue_note[i];
    char utf8[512]{};
    if (!WideCharToMultiByte(CP_UTF8, 0, note, -1, utf8, sizeof(utf8), nullptr, nullptr))
        return;
    for (char &ch : utf8)
        if (ch == '\r' || ch == '\n' || ch == '\t') ch = ' ';
    std::fprintf(output, "ISSUE-NOTE %s\n", utf8[0] ? utf8 : "(none)");
}

// Observe host mouse movement only while recording. The hook reads input but
// never modifies or consumes a message delivered to the game.
LRESULT CALLBACK raw_record_hook(int code, WPARAM wparam, LPARAM lparam) {
    FILE *recording = g_record_file;
    if (code >= 0 && wparam == PM_REMOVE && lparam && recording &&
        (g_record_player == 1 || g_record_player == 3)) {
        const auto *message = reinterpret_cast<const MSG *>(lparam);
        if ((message->message == WM_INPUT || message->message == WM_MOUSEMOVE) &&
            (message->hwnd == g_window || (message->hwnd && IsChild(g_window, message->hwnd)))) {
            static ULONGLONG last_raw = 0, last_legacy = 0;
            static long sum_x = 0, sum_y = 0;
            static unsigned samples = 0, flags = 0;
            const ULONGLONG now = GetTickCount64();
            if (message->message == WM_INPUT) {
                RAWINPUT input{};
                UINT bytes = sizeof(input);
                const UINT result = GetRawInputData(reinterpret_cast<HRAWINPUT>(message->lParam),
                    RID_INPUT, &input, &bytes, sizeof(RAWINPUTHEADER));
                if (result != static_cast<UINT>(-1) && input.header.dwType == RIM_TYPEMOUSE &&
                    (input.data.mouse.lLastX || input.data.mouse.lLastY)) {
                    sum_x += input.data.mouse.lLastX;
                    sum_y += input.data.mouse.lLastY;
                    flags |= input.data.mouse.usFlags;
                    ++samples;
                    if (!last_raw || now - last_raw >= 250) {
                        std::fprintf(recording,
                            "P1-RAW-MOUSE t=%llu dx=%ld dy=%ld events=%u flags=%u\n",
                            static_cast<unsigned long long>(now), sum_x, sum_y, samples, flags);
                        lockon13beta::diagnostic_orientation(0, recording);
                        sum_x = sum_y = 0;
                        samples = flags = 0;
                        last_raw = now;
                    }
                }
            } else if (message->message == WM_MOUSEMOVE &&
                now - last_raw > 1000 && now - last_legacy >= 250) {
                std::fprintf(recording, "P1-CURSOR-MOVE t=%llu x=%d y=%d\n",
                    static_cast<unsigned long long>(now),
                    static_cast<short>(LOWORD(message->lParam)),
                    static_cast<short>(HIWORD(message->lParam)));
                last_legacy = now;
            }
        }
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

LRESULT CALLBACK hook_proc(int code, WPARAM wparam, LPARAM lparam) {
    if (code >= 0 && lparam) {
        const auto *message = reinterpret_cast<const CWPSTRUCT *>(lparam);
        if (message->hwnd == g_window && g_log &&
            (message->message == WM_ACTIVATEAPP || message->message == WM_SETFOCUS ||
             message->message == WM_KILLFOCUS)) {
            std::fprintf(g_log, "Game focus event t=%llu message=%u active=%llu\n",
                static_cast<unsigned long long>(GetTickCount64()),
                static_cast<unsigned>(message->message),
                static_cast<unsigned long long>(message->wParam));
            if (g_record_file) std::fprintf(g_record_file,
                "GAME-FOCUS t=%llu message=%u active=%llu\n",
                static_cast<unsigned long long>(GetTickCount64()),
                static_cast<unsigned>(message->message),
                static_cast<unsigned long long>(message->wParam));
            std::fflush(g_log);
        }
        if (message->hwnd == g_window && message->message == g_message) {
            __try {
                if (message->wParam == 1) release_held_keys();
                else if (message->wParam == 2) log_input_snapshot();
                else if (message->wParam == 3 && g_record_file) {
                    if (g_record_player == 3) {
                        std::fprintf(g_record_file, "=== BOTH PLAYERS SAMPLE t=%llu ===\n",
                            static_cast<unsigned long long>(GetTickCount64()));
                        log_input_snapshot(1, g_record_file);
                        log_input_snapshot(2, g_record_file);
                    } else log_input_snapshot(g_record_player, g_record_file);
                }
                else if (message->wParam == 4 && g_record_file) {
                    const LONG key = g_settings
                        ? InterlockedCompareExchange(&g_settings->issue_marker_vk, 0, 0) : VK_NUMPAD0;
                    std::fprintf(g_record_file, "=== ISSUE MARKER vk=%ld t=%llu ===\n",
                        key, static_cast<unsigned long long>(GetTickCount64()));
                    log_issue_note(g_record_file);
                    if (g_record_player == 1 || g_record_player == 3)
                        log_input_snapshot(1, g_record_file);
                    if (g_record_player == 2 || g_record_player == 3)
                        log_input_snapshot(2, g_record_file);
                }
                else if (message->wParam == 5) { /* recording close barrier */ }
                else {
                    LARGE_INTEGER frequency{}, begin{}, after_lock{}, after_input{};
                    const bool profile = g_record_file != nullptr;
                    if (profile) { QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&begin); }
                    lockon13beta::update();
                    if (profile) QueryPerformanceCounter(&after_lock);
                    handle_batch();
                    repair_player1_alt_after_focus();
                    // Arm and toggle changes apply on the next callback. A second
                    // lock-on pass here repeats work on the game's input thread.
                    if (profile && frequency.QuadPart > 0) {
                        QueryPerformanceCounter(&after_input);
                        static unsigned samples = 0;
                        static unsigned long long lock_sum = 0, input_sum = 0;
                        static unsigned long long lock_max = 0, input_max = 0;
                        static ULONGLONG last_report = 0;
                        const auto lock_us = static_cast<unsigned long long>(
                            (after_lock.QuadPart - begin.QuadPart) * 1000000 / frequency.QuadPart);
                        const auto input_us = static_cast<unsigned long long>(
                            (after_input.QuadPart - after_lock.QuadPart) * 1000000 / frequency.QuadPart);
                        ++samples; lock_sum += lock_us; input_sum += input_us;
                        if (lock_us > lock_max) lock_max = lock_us;
                        if (input_us > input_max) input_max = input_us;
                        const auto now = GetTickCount64();
                        if (!last_report) last_report = now;
                        if (now - last_report >= 1000) {
                            std::fprintf(g_record_file,
                                "BRIDGE-PERF t=%llu samples=%u lock_us_avg=%llu max=%llu input_us_avg=%llu max=%llu\n",
                                static_cast<unsigned long long>(now), samples,
                                lock_sum / samples, lock_max, input_sum / samples, input_max);
                            samples = 0; lock_sum = input_sum = lock_max = input_max = 0;
                            last_report = now;
                        }
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                ++g_errors;
                std::fprintf(g_log, "Game input dispatch raised an exception\n");
            }
        }
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

void close_recording() {
    FILE *closing = g_record_file;
    g_record_file = nullptr;
    if (g_raw_record_hook) {
        UnhookWindowsHookEx(g_raw_record_hook);
        g_raw_record_hook = nullptr;
    }
    if (!closing) return;
    DWORD_PTR ignored = 0;
    SendMessageTimeoutW(g_window, g_message, 5, 0, SMTO_ABORTIFHUNG, 1000, &ignored);
    std::fprintf(closing, "Recording stopped t=%llu\n",
        static_cast<unsigned long long>(GetTickCount64()));
    std::fclose(closing);
}

BOOL CALLBACK find_window(HWND window, LPARAM parameter) {
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(window) && GetWindow(window, GW_OWNER) == nullptr) {
        *reinterpret_cast<HWND *>(parameter) = window;
        return FALSE;
    }
    return TRUE;
}

DWORD run_bridge(void *self) {
    g_self = static_cast<HMODULE>(self);
    for (auto &pending : g_pending) pending.active = false;
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(g_self, path, MAX_PATH)) return 1;
    wchar_t *slash = wcsrchr(path, L'\\');
    if (!slash) return 2;
    wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"HalfSwordBridge13Beta.log");
    g_log = _wfsopen(path, L"w", _SH_DENYNO);
    if (!g_log) return 3;
    std::fprintf(g_log, "Half Sword Player 2 KBM Bridge 1.3 Lock On Unstable Beta; remote C disabled; PID=%lu\n", GetCurrentProcessId());
    g_settings_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
        sizeof(BridgeSettings), L"Local\\HalfSwordBridgeSettingsV13Beta");
    if (g_settings_mapping) {
        const bool created = GetLastError() != ERROR_ALREADY_EXISTS;
        g_settings = static_cast<BridgeSettings *>(MapViewOfFile(g_settings_mapping,
            FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(BridgeSettings)));
        if (g_settings && created) InterlockedExchange(&g_settings->sensitivity_per_thousand, 250);
    }
    for (int slot = 0; slot < kBindingCount; ++slot)
        std::fprintf(g_log, "Joiner binding %d physical=%ld action=%d\n", slot,
            g_settings ? InterlockedCompareExchange(&g_settings->bindings[slot], 0, 0)
                : kDefaultBindings[slot], kCanonicalBindings[slot]);
    std::fflush(g_log);
    std::fprintf(g_log, "Remote mouse sensitivity defaults to 25%%; host slider can adjust it\n");
    EnumWindows(find_window, reinterpret_cast<LPARAM>(&g_window));
    if (!g_window) { std::fprintf(g_log, "Game window not found\n"); std::fclose(g_log); return 4; }
    const DWORD thread = GetWindowThreadProcessId(g_window, nullptr);
    g_message = RegisterWindowMessageW(L"HalfSwordSteamRemotePlayer2BridgeV20_20260917");
    HHOOK hook = g_message ? SetWindowsHookExW(WH_CALLWNDPROC, hook_proc, g_self, thread) : nullptr;
    if (!hook) { std::fprintf(g_log, "Game thread unavailable: %lu\n", GetLastError()); std::fclose(g_log); return 5; }
    HMODULE steam = GetModuleHandleW(L"steam_api64.dll");
    using Init = bool (*)();
    using User = int (*)();
    using Find = void *(*)(int, const char *);
    auto init = steam ? reinterpret_cast<Init>(GetProcAddress(steam, "SteamAPI_Init")) : nullptr;
    auto user = steam ? reinterpret_cast<User>(GetProcAddress(steam, "SteamAPI_GetHSteamUser")) : nullptr;
    auto find = steam ? reinterpret_cast<Find>(GetProcAddress(steam, "SteamInternal_FindOrCreateUserInterface")) : nullptr;
    ISteamRemotePlay *remote = nullptr;
    if (init && user && find && (user() || init()))
        remote = static_cast<ISteamRemotePlay *>(find(user(), STEAMREMOTEPLAY_INTERFACE_VERSION));
    if (!remote || !remote->BEnableRemotePlayTogetherDirectInput()) {
        std::fprintf(g_log, "Steam direct input unavailable\n");
        UnhookWindowsHookEx(hook); std::fclose(g_log); return 6;
    }
    std::fprintf(g_log, "Steam direct input enabled; window thread=%lu\n", thread);
    std::fflush(g_log);
    HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, L"Local\\HalfSwordBridge13BetaStop");
    HANDLE running = CreateEventW(nullptr, TRUE, TRUE, L"Local\\HalfSwordBridge13BetaRunning");
    HANDLE reset = CreateEventW(nullptr, TRUE, FALSE, L"Local\\HalfSwordBridge13BetaReset");
    HANDLE snapshot = CreateEventW(nullptr, TRUE, FALSE, L"Local\\HalfSwordBridge13BetaSnapshot");
    ULONGLONG last_record_sample = 0;
    bool have_session = false;
    uint32 session = 0;
    unsigned recenter_count = 0;
    ULONGLONG last_recenter = 0;
    ULONGLONG recenter_time = 0;
    ULONGLONG last_session_check = 0;
    bool marker_was_down = false;
    ULONGLONG last_timing_log = 0;
    unsigned timing_polls = 0, timing_events = 0, timing_full_batches = 0;
    ULONGLONG timing_poll_sum = 0, timing_dispatch_sum = 0;
    ULONGLONG timing_poll_max = 0, timing_dispatch_max = 0;
    while (!stop || WaitForSingleObject(stop, 0) != WAIT_OBJECT_0) {
        const LONG wanted_player = g_settings
            ? InterlockedCompareExchange(&g_settings->recording_player, 0, 0) : 0;
        if (wanted_player != g_record_player) {
            close_recording();
            g_record_player = wanted_player >= 1 && wanted_player <= 3 ? wanted_player : 0;
            if (g_record_player) {
                wchar_t record_path[MAX_PATH]{};
                wcscpy_s(record_path, path);
                wchar_t *record_slash = wcsrchr(record_path, L'\\');
                if (record_slash) {
                    swprintf_s(record_slash + 1, MAX_PATH - (record_slash + 1 - record_path),
                        L"HalfSwordRecording-%ls-%llu.log",
                        g_record_player == 3 ? L"Both" : (g_record_player == 1 ? L"P1" : L"P2"),
                        static_cast<unsigned long long>(GetTickCount64()));
                    g_record_file = _wfsopen(record_path, L"w", _SH_DENYNO);
                }
                if (g_record_file) {
                    std::fprintf(g_record_file, "Half Sword input recording 1.3 Lock On Unstable Beta; Player selection %d (3=both); game PID=%lu\n"
                        "Includes mouse movement, bridge input events, lock-on orientation, and bridge-side Remote Play timing. Network ping is unavailable from the Steam input API. No screen or audio is captured.\n",
                        g_record_player, GetCurrentProcessId());
                    log_issue_note(g_record_file);
                    std::fflush(g_record_file);
                    if (g_record_player == 1 || g_record_player == 3) {
                        g_raw_record_hook = SetWindowsHookExW(WH_GETMESSAGE, raw_record_hook, g_self, thread);
                        std::fprintf(g_log, "Player 1 mouse observer %s; error=%lu\n",
                            g_raw_record_hook ? "active" : "unavailable",
                            g_raw_record_hook ? 0 : GetLastError());
                    }
                    std::fprintf(g_log, "Recording Player %d to file beside app\n", g_record_player);
                } else {
                    std::fprintf(g_log, "Could not create Player %d recording file\n", g_record_player);
                    g_record_player = 0;
                }
                std::fflush(g_log);
            }
        }
        if (g_settings && wanted_player == 0 && g_record_player == 0 && !g_record_file) {
            // Acknowledge only after close_recording() has flushed and closed the file.
            const LONG request = InterlockedCompareExchange(&g_settings->recording_close_request, 0, 0);
            InterlockedExchange(&g_settings->recording_close_ack, request);
        }
        bool marker_down = false;
        if (g_record_file && g_settings &&
            InterlockedCompareExchange(&g_settings->issue_marker_enabled, 0, 0)) {
            DWORD focused_pid = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &focused_pid);
            const LONG key = InterlockedCompareExchange(&g_settings->issue_marker_vk, 0, 0);
            // Windows reports the physical keypad 0 as Insert when Num Lock is off.
            const bool keypad_insert = key == VK_NUMPAD0 &&
                (GetKeyState(VK_NUMLOCK) & 1) == 0 &&
                (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
            marker_down = focused_pid == GetCurrentProcessId() &&
                key >= VK_LBUTTON && key <= 254 && key != VK_F12 &&
                ((GetAsyncKeyState(key) & 0x8000) != 0 || keypad_insert);
        }
        if (marker_down && !marker_was_down) {
            DWORD_PTR ignored = 0;
            SendMessageTimeoutW(g_window, g_message, 4, 0, SMTO_ABORTIFHUNG, 1000, &ignored);
        }
        marker_was_down = marker_down;
        const ULONGLONG sample_time = GetTickCount64();
        if (g_record_file && sample_time - last_record_sample >= 1000) {
            DWORD_PTR ignored = 0;
            SendMessageTimeoutW(g_window, g_message, 3, 0, SMTO_ABORTIFHUNG, 1000, &ignored);
            last_record_sample = sample_time;
        }
        if (snapshot && WaitForSingleObject(snapshot, 0) == WAIT_OBJECT_0) {
            DWORD_PTR ignored = 0;
            SendMessageTimeoutW(g_window, g_message, 2, 0, SMTO_ABORTIFHUNG, 1000, &ignored);
            ResetEvent(snapshot);
        }
        if (reset && WaitForSingleObject(reset, 0) == WAIT_OBJECT_0) {
            DWORD_PTR ignored = 0;
            SendMessageTimeoutW(g_window, g_message, 1, 0, SMTO_ABORTIFHUNG, 1000, &ignored);
            ResetEvent(reset);
            std::fprintf(g_log, "Manual reset of held Player 2 inputs\n");
            std::fflush(g_log);
        }
        const ULONGLONG loop_start = GetTickCount64();
        if (have_session && loop_start - last_session_check >= 1000) {
            last_session_check = loop_start;
            bool connected = false;
            const uint32 sessions = remote->GetSessionCount();
            for (uint32 index = 0; index < sessions; ++index)
                if (remote->GetSessionID(static_cast<int>(index)) == session) connected = true;
            if (!connected) {
                DWORD_PTR ignored = 0;
                SendMessageTimeoutW(g_window, g_message, 1, 0, SMTO_ABORTIFHUNG, 1000, &ignored);
                std::fprintf(g_log, "Remote session %u disconnected; held keys released\n", session);
                std::fflush(g_log);
                have_session = false;
                session = 0;
            }
        }
        RemotePlayInput_t incoming[64];
        const ULONGLONG poll_start = GetTickCount64();
        const uint32 count = remote->GetInput(incoming, 64);
        const ULONGLONG poll_ms = GetTickCount64() - poll_start;
        if (count == 64 && g_full_input_batches++ < 8) {
            std::fprintf(g_log, "Steam input batch filled all 64 slots; draining queue without frame delay\n");
            std::fflush(g_log);
        }
        g_count = 0;
        bool near_edge = false;
        for (uint32 i = 0; i < count; ++i) {
            if (!have_session) { session = incoming[i].m_unSessionID; have_session = true;
                remote->SetMouseVisibility(session, false);
                std::fprintf(g_log, "Receiving remote session=%u; remote cursor hidden\n", session);
                std::fflush(g_log); }
            if (incoming[i].m_unSessionID != session) continue;
            if (incoming[i].m_eType == k_ERemotePlayInputMouseMotion) {
                const auto &motion = incoming[i].m_MouseMotion;
                const ULONGLONG now = GetTickCount64();
                if (recenter_time && now - recenter_time < 150 &&
                    motion.m_flNormalizedX > 0.45f && motion.m_flNormalizedX < 0.55f &&
                    motion.m_flNormalizedY > 0.45f && motion.m_flNormalizedY < 0.55f &&
                    (motion.m_nDeltaX > 50 || motion.m_nDeltaX < -50 ||
                     motion.m_nDeltaY > 50 || motion.m_nDeltaY < -50)) continue;
                near_edge = near_edge || (motion.m_bAbsolute &&
                    (motion.m_flNormalizedX < 0.10f || motion.m_flNormalizedX > 0.90f ||
                     motion.m_flNormalizedY < 0.10f || motion.m_flNormalizedY > 0.90f));
                // Combine only adjacent motion events. A button or key event
                // always ends the group, preserving the order of combat input.
                if (g_count && g_events[g_count - 1].m_eType == k_ERemotePlayInputMouseMotion) {
                    auto &previous = g_events[g_count - 1].m_MouseMotion;
                    const auto sum_x = static_cast<long long>(previous.m_nDeltaX) + motion.m_nDeltaX;
                    const auto sum_y = static_cast<long long>(previous.m_nDeltaY) + motion.m_nDeltaY;
                    if (sum_x >= INT_MIN && sum_x <= INT_MAX && sum_y >= INT_MIN && sum_y <= INT_MAX) {
                        previous.m_nDeltaX = static_cast<int>(sum_x);
                        previous.m_nDeltaY = static_cast<int>(sum_y);
                        previous.m_flNormalizedX = motion.m_flNormalizedX;
                        previous.m_flNormalizedY = motion.m_flNormalizedY;
                        previous.m_bAbsolute = motion.m_bAbsolute;
                        ++g_motion_merged;
                        continue;
                    }
                }
            }
            g_events[g_count++] = incoming[i];
        }
        const ULONGLONG dispatch_start = GetTickCount64();
        {
            DWORD_PTR result = 0;
            if (!SendMessageTimeoutW(g_window, g_message, 0, 0, SMTO_ABORTIFHUNG, 1000, &result)) {
                std::fprintf(g_log, "Game thread stopped accepting events: %lu\n", GetLastError());
                break;
            }
        }
        const ULONGLONG dispatch_ms = GetTickCount64() - dispatch_start;
        if (g_record_file && have_session) {
            ++timing_polls;
            timing_events += g_count;
            if (count == 64) ++timing_full_batches;
            timing_poll_sum += poll_ms;
            timing_dispatch_sum += dispatch_ms;
            if (poll_ms > timing_poll_max) timing_poll_max = poll_ms;
            if (dispatch_ms > timing_dispatch_max) timing_dispatch_max = dispatch_ms;
            const ULONGLONG stamp = GetTickCount64();
            if (!last_timing_log) last_timing_log = stamp;
            if (stamp - last_timing_log >= 1000) {
                std::fprintf(g_record_file,
                    "REMOTE-TIMING t=%llu session=%u polls=%u events=%u full_batches=%u "
                    "steam_get_input_ms_avg=%llu max=%llu game_dispatch_ms_avg=%llu max=%llu mouse_events_merged=%u ping_ms=unavailable\n",
                    static_cast<unsigned long long>(stamp), session, timing_polls, timing_events,
                    timing_full_batches,
                    static_cast<unsigned long long>(timing_poll_sum / timing_polls),
                    static_cast<unsigned long long>(timing_poll_max),
                    static_cast<unsigned long long>(timing_dispatch_sum / timing_polls),
                    static_cast<unsigned long long>(timing_dispatch_max), g_motion_merged);
                timing_polls = timing_events = timing_full_batches = 0;
                g_motion_merged = 0;
                timing_poll_sum = timing_dispatch_sum = timing_poll_max = timing_dispatch_max = 0;
                last_timing_log = stamp;
            }
        } else if (!g_record_file) {
            timing_polls = timing_events = timing_full_batches = 0;
            timing_poll_sum = timing_dispatch_sum = timing_poll_max = timing_dispatch_max = 0;
            last_timing_log = 0;
        }
        // Test 49: request more frequent game-thread turns. The lock-on
        // diagnostics report the actual cadence achieved on this host.
        Sleep(count == 64 ? 0 : 1);
        const ULONGLONG now = GetTickCount64();
        if (near_edge && now - last_recenter >= 100) {
            remote->SetMousePosition(session, 0.5f, 0.5f);
            last_recenter = now;
            recenter_time = now;
            ++recenter_count;
            if (recenter_count <= 5) {
                std::fprintf(g_log, "Recentering remote cursor after edge hit (%u)\n", recenter_count);
                std::fflush(g_log);
            }
        }
        if (g_errors) { std::fprintf(g_log, "Stopping after dispatch exception\n"); break; }
    }
    DWORD_PTR cleanup_result = 0;
    SendMessageTimeoutW(g_window, g_message, 1, 0, SMTO_ABORTIFHUNG, 1000, &cleanup_result);
    if (have_session) remote->SetMouseVisibility(session, true);
    remote->DisableRemotePlayTogetherDirectInput();
    std::fprintf(g_log, "Direct input disabled; keyboard=%u mouse=%u buttons=%u ignored=%u repeats_suppressed=%u errors=%u recentered=%u\n",
        g_keys, g_mouse, g_buttons, g_dropped, g_repeat_suppressed, g_errors, recenter_count);
    std::fflush(g_log);
    close_recording();
    UnhookWindowsHookEx(hook);
    if (stop) CloseHandle(stop);
    if (running) CloseHandle(running);
    if (reset) CloseHandle(reset);
    if (snapshot) CloseHandle(snapshot);
    if (g_settings) UnmapViewOfFile(g_settings);
    if (g_settings_mapping) CloseHandle(g_settings_mapping);
    std::fclose(g_log);
    return 0;
}

DWORD WINAPI worker(void *self) {
    HANDLE ready = CreateEventW(nullptr, TRUE, TRUE, L"Local\\HalfSwordBridge13BetaReady");
    HANDLE start = CreateEventW(nullptr, TRUE, FALSE, L"Local\\HalfSwordBridge13BetaStart");
    if (!ready || !start) return 1;
    for (;;) {
        run_bridge(self);
        if (WaitForSingleObject(start, INFINITE) != WAIT_OBJECT_0) break;
        ResetEvent(start);
    }
    CloseHandle(start);
    CloseHandle(ready);
    return 0;
}
}

extern "C" __declspec(dllexport) LRESULT CALLBACK HalfSwordBootstrapHook(
    int code, WPARAM wparam, LPARAM lparam) {
    // Keep a durable module reference after the launcher removes its temporary
    // Windows message hook.
    static INIT_ONCE pinned_once = INIT_ONCE_STATIC_INIT;
    InitOnceExecuteOnce(&pinned_once,
        [](PINIT_ONCE, PVOID parameter, PVOID *) -> BOOL {
            HMODULE pinned = nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                reinterpret_cast<LPCWSTR>(parameter), &pinned);
            return TRUE;
        }, reinterpret_cast<PVOID>(&HalfSwordBootstrapHook), nullptr);
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        wchar_t process_path[MAX_PATH]{};
        if (GetModuleFileNameW(nullptr, process_path, MAX_PATH)) {
            const wchar_t *name = wcsrchr(process_path, L'\\');
            name = name ? name + 1 : process_path;
            if (_wcsicmp(name, L"HalfSwordUE5-Win64-Shipping.exe") == 0) {
                HANDLE thread = CreateThread(nullptr, 0, worker, module, 0, nullptr);
                if (thread) CloseHandle(thread);
            }
        }
    }
    return TRUE;
}
