Half Sword Remote KBM Bridge — 1.3 Lock On Unstable Beta

This beta includes the experimental lock-on system. Bugs, control issues, and performance problems may occur. Please send reports using Bug Reporting Guide.txt.

Setup
1. Extract the complete ZIP into its own folder.
2. Keep HalfSwordJoinerControls13Beta.exe and HalfSwordBridge13Beta.dll together. Do not rename them.
3. Close other bridge controllers and restart Half Sword before switching versions.
4. For remote Player 2, start local split screen and invite the joiner through Steam Remote Play Together.
5. On the host computer, open HalfSwordJoinerControls13Beta.exe and click Start.

The implementation targets Half Sword Early Access 0.6.15 for Windows. Compatibility with later game versions has not been verified.

Controls tab: Start/Stop, joiner mouse sensitivity, Alt mouse behavior, reset stuck Player 2 inputs, and optional mirroring of the host's G hold to Player 2.
Lock On tab: main enable switch, separate player switches, and a requested update interval of 1, 2, 4, 8, or 16 ms. Default: 1 ms. Smaller intervals can increase workload. Actual update timing depends on game callbacks.
Record tab: recording selection, issue marker, and issue description.
Keybinds tab: stop the bridge, select a joiner action, click Press to bind, and press the desired key or mouse button. Duplicate bindings and Shift are rejected. Caps Lock is the default joiner sprint key.

Lock-on defaults: Z toggles that player's custom lock-on; Tab switches target while either mouse button is held. Holding either mouse button allows lock-on turning; releasing both disengages it. A downed or dead target should be dropped. Please report failures of these behaviors. Player 2's toggle and cycle keys can be rebound on Keybinds.

This beta is based on Test 49. The beta package compiled successfully; its packaging is not a claim of complete multiplayer or compatibility testing.
