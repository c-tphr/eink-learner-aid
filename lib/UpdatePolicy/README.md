# UpdatePolicy

Portable rules for the firmware updating itself (task D4; spike S3, sections 6 and 7). No Arduino,
ESP-IDF or network code; host-tested in `test/update_policy`, which reads the repository's real
partition tables. `src/update/UpdateGate.cpp` feeds them from the ESP-IDF partition API under the
feature flag. Rules and names: `docs/TWO_SLOT.md`.

| File | What it holds |
| --- | --- |
| `SlotPolicy.h/.cpp` | `decide` (may this table self-update?), `checkWrite` (may this partition be written as the update?), `Verdict`, `verdictName` |
| `ReleaseNaming.h/.cpp` | `LATEST_RELEASE_URL` (this repository), `formatAssetName` (`eink-learner-aid-<tag>-picco2.bin`), `parseVersion`, `isNewerRelease` |
