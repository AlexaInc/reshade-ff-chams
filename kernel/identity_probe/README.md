# Kernel build bootstrap — not a memory reader

This isolated WDM C++ project checks that GitHub Actions can build a 64-bit Windows kernel binary using the WDK. It is deliberately inert: it creates no device or IOCTL interface, opens no process, reads no memory, identifies no players and makes no renderer changes.

The output is **unsigned**, is not a deployable product, and must not be installed or loaded. Building is not signing. The gaming PC's Secure Boot and protections must remain unchanged. No installation steps are provided.

The GitHub Actions workflow builds on `windows-2022` with WDK/SDK 10.0.26100.0, verifies the output is unsigned, and uploads it briefly as a build artifact. It does not alter the existing chams build or publish releases.

Future milestones require: an authorized signing route accepted under the target Windows policy; a separately reviewed, narrowly scoped access design; live evidence of the guest-to-host mapping and game layout for an exact build; and demonstrated entity-to-draw correlation. None is established by this bootstrap. Do not create a general-purpose arbitrary physical/kernel-memory IOCTL or attempt to defeat system or game protections.
