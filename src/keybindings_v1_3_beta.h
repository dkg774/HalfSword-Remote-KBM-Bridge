#pragma once

// Remote physical inputs are Steam USB HID keyboard codes or 256 + mouse button bit.
// The last two entries are consumed by the custom lock-on layer and are never
// forwarded to Half Sword's ordinary input graph.
constexpr int kBindingCount = 24;
constexpr LONG kMouseBindingBase = 256;
constexpr LONG kDefaultBindings[kBindingCount] = {
    26, 22, 4, 7,       // movement: W S A D
    20, 8, 44, 57,      // left grab, right grab, jump, sprint
    224, 226, 27, 10,   // crouch, thrust, swap, give up
    9, 21, 25, 0,       // slow motion, inventory, camera, old camera lock disabled
    41, 11,             // pause (Escape), undress
    257, 258, 272,      // left mouse/right hand, right mouse/left hand, middle mouse
    19,                  // secondary pause key (P)
    29, 43               // custom lock toggle (Z), target cycle (Tab)
};
constexpr int kCanonicalBindings[kBindingCount] = {
    26, 22, 4, 7, 20, 8, 44, 57, 224, 226, 27, 10,
    9, 21, 25, 0, 41, 11, 257, 258, 272, 41, 29, 43
};
constexpr const wchar_t *kBindingActions[kBindingCount] = {
    L"Move forward", L"Move backward", L"Move left", L"Move right",
    L"Grab left", L"Grab right", L"Jump", L"Sprint",
    L"Crouch", L"Thrust / Classic Alt", L"Swap hands", L"Give up / taunt",
    L"Slow motion", L"Inventory", L"Change camera", L"Vanilla camera lock (unused by custom lock-on)",
    L"Pause", L"Undress", L"Right-hand mouse", L"Left-hand mouse",
    L"Middle-mouse action", L"Pause (second key)",
    L"Custom lock-on toggle", L"Cycle lock-on target"
};
struct BridgeSettings {
    LONG sensitivity_per_thousand;
    LONG alt_native_mouse;
    LONG recording_player;
    LONG lockon_available;
    LONG lockon_player1;
    LONG lockon_player2;
    LONG bindings[kBindingCount];
    LONG host_g_mirrors_player2;
    LONG issue_marker_enabled;
    LONG issue_marker_vk;
    wchar_t issue_note[128];
    LONG recording_close_request;
    LONG recording_close_ack;
    LONG lockon_cadence_ms;
};
