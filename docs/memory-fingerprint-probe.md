# Separate opt-in fingerprint probe

This is a separate add-on loaded by the existing ReShade add-on mechanism. It does not modify or replace `opengl32.dll`, register rendering callbacks, use a kernel driver, write to target memory, or change Secure Boot. It does not collect or dump process memory contents and does not read player lists.

After registration, a cancellable background worker creates a PID/nonce-specific `chams-fingerprint-ready-<pid>-<token>.txt` in `%TEMP%`. Nothing is scanned until the corresponding `chams-fingerprint-trigger-<pid>-<token>.flag` file appears. After one scan the worker writes a new `chams-fingerprint-result-<pid>-<token>.txt`; it never overwrites results and stops promptly when ReShade unloads the add-on. Repeat initializations create new tokens.

The scan walks readable committed host-process regions and copies at most 64 MiB in 64 KiB chunks, with a 10-second cap and 4 ms pause per chunk. It only compares a 64-byte ELF header fingerprint for the installed 2.132.1 ARM64 `libil2cpp.so`, whose disk SHA-256 was independently matched to the offline file `fa26f9926bd61c8722b453c69d5ebb748b8da3764911c24c546a1f8f3e656de6`. It excludes the probe DLL's own allocation and its scan buffer, reports counts and one candidate host address, and records no matched contents.

A match can be a cache or copy, and is **not evidence of a guest virtual-address mapping or player identity**. A negative scan is inconclusive because the budget covers only part of the address space. The add-on may impact game performance or trigger game enforcement; install and trigger only with the user's permission. Removing only this add-on while BlueStacks is closed disables it.
