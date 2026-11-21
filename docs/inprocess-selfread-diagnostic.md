# Opt-in in-process self-read diagnostic

This is a diagnostic ReShade build, not a player detector. On the first chams initialization only, if `%APPDATA%\reshade\chams\memory-selftest.enable` is a file, the DLL uses the current-process handle to read a four-byte marker it owns and the two-byte executable image signature. It records only booleans, error codes and process ID; it does not dump memory, inspect game entities, scan address ranges, access other processes, modify guest state, or change rendering logic.

The result is logged once as `[CHAMS_MEMORY_SELFTEST]` in a new PID-specific `%TEMP%\chams-selftest-<pid>.txt` file and in ReShade.log when available. The file is opened with `CREATE_NEW`, so an existing result or the regular chams dump is never truncated. `host_hd_player=1` confirms the host executable name; `marker_ok=1` and `image_ok=1` confirm only those two host reads. They do **not** establish access to Android guest memory, guest-to-host translation, entity layout, or draw identity. Runtime access has not yet been validated in BlueStacks.

To disable the diagnostic, remove `memory-selftest.enable` before launching the emulator. This branch does not load a driver or change Secure Boot. Back up the user's active DLL before any manual test; never replace it while the process has it loaded. Normal chams tracking remains unchanged.
