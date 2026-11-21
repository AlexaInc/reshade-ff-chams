# Separate resumable fingerprint probe

This is a separate add-on loaded by the existing ReShade add-on mechanism; it does not modify `opengl32.dll`, register graphics callbacks, change Secure Boot or use a kernel driver. No process memory contents are dumped, no other process is read, and no player identity is extracted.

The worker waits for a PID/nonce-specific trigger file in `%TEMP%`. After one trigger it scans up to eight 64 MiB slices of readable host-process memory (512 MiB attempted in total), with 10 seconds maximum per slice, four-millisecond pauses per 64 KiB chunk, and two-second pauses between slices. It resumes from the previous cursor with a small overlap to catch a fingerprint across a slice boundary. It is cancellable on add-on unload. A result file records counts and candidate addresses, not raw memory. Logs and triggers use unique PID/nonce names and never overwrite existing files.

The compared fingerprint is the first 64 bytes of the previously hash-matched installed 2.132.1 ARM64 `libil2cpp.so`. A match might be a cache or other copy, not proof of guest mapping or identity. A negative result is inconclusive because even 512 MiB may be only a fraction of the host address space. The scan may affect game performance or trigger game enforcement; install and trigger only with the user's permission.

Unit tests cover a match crossing a slice boundary, continuation progress, inaccessible pages and cancellation. A mock-host test covers registration, trigger and safe cancellation/unload. Removing only `MemoryResumeProbe.addon64` while BlueStacks is closed disables the add-on.
