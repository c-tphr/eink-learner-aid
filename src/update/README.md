# src/update

**Self-update gate** (task D4, #34; spike S3, sections 6 and 7; `docs/TWO_SLOT.md`). Decides from
the running partition table whether "Check for updates", "SD Card Firmware Update" and the boot
recovery picker are offered, checks every update write at the point of writing (never the running
slot, only the next update slot, only on a two-slot table), and points the Wi-Fi updater at this
repository's releases. The rules are in `lib/UpdatePolicy`; this directory reads the table through
ESP-IDF and is called from four upstream registration points (`docs/ARCHITECTURE-RSVP.md`).

Everything here is compiled only with the RSVP feature flag: `UpdateGate.cpp` is wrapped in
`#if CROSSPOINT_RSVP_ENABLED`, and `UpdateGate.h` returns upstream's own values without the flag.
In the simulator (`SIMULATOR`) updates are never offered or written.
