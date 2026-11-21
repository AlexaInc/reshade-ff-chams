# Separate in-process memory add-on diagnostic

This branch adds a standalone `MemoryIdentityProbe.addon64`; it does not modify or replace `opengl32.dll`, ReShade or the existing Chams renderer. ReShade's existing add-on loader loads the separate module. It registers no OpenGL/render events and installs no graphics or memory hooks. Removing the add-on and restarting the host disables it.

At add-on initialization it uses the current-process handle to read only a known local marker and the first two bytes of the host executable image. It writes booleans, error codes and process ID to a new `%TEMP%\chams-memory-addon-selftest-<pid>.txt` file and, where available, the ReShade log. The result file is `CREATE_NEW`, never overwriting existing captures. There is no scanning, other-process access, data dump, network communication, guest memory read, identity classification, or modification of target memory.

`host_hd_player=1` says that the executable name is HD-Player.exe. `marker_ok=1` and `image_ok=1` only prove these two local reads; they do not prove guest-memory mapping, player-list availability, or draw correlation. A GitHub Actions mock host tests load/registration/self-read/unload. BlueStacks runtime is not yet tested.

Secure Boot is unchanged; no driver is used. Installation must be explicitly approved and requires placing the new add-on into the configured add-on search directory, without touching existing DLLs. Remove that one add-on file when the host is stopped to disable the test.
