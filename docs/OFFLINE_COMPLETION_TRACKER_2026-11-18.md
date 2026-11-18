# MPK225 offline completion tracker

Updated 2026-11-18. Scope: `Phaos2000/mpk225-openklave`, branch `development/v0.2-chord-pipeline`.

## Bounded tasks and acceptance evidence

| Task | Acceptance evidence | Status |
|---|---|---|
| Confirm current source and branch | Clean checkout at `89d40be6ba80eef725dd53fa291ce1b87f51da9e`, matching `origin/development/v0.2-chord-pipeline`; no newer branch commit found. | Complete |
| Verify integrated host behavior | CI run [#162](https://github.com/Phaos2000/mpk225-openklave/actions/runs/43107778609), job `host-and-arm`, passed the complete `tests/test_*.cpp` suite, Python patch/ELF/USB-probe checks, enabled chord/panel/full-path variants, and native MIDI bridge self-test. | Complete (CI) |
| Verify Windows host integration | Same CI run, job `windows-live-bridge`, passed WinMM build and offline self-test; produced the read-only/live-chord test executable artifact. | Complete (CI) |
| Verify embedded integration and image guards | Same CI run, `host-and-arm`: Cortex-M3 USB compile, pinned OpenKlave integration compile, freestanding ARM build, guarded ELF variants, signature/load-range checks, and confirmation that firmware release actions remain disabled all passed. | Complete (CI) |
| Review acceptance gaps and recovery limits | `COMPLETION_CHECKLIST.md`, `LIVE_STOCK_MPK225_TEST.md`, `DIN_ROUTE_PASSIVE_AUDIT.md`, and `BOOTLOADER_RECOVERY_PREFLIGHT.md` define read-only observations and prohibit firmware writes absent a nondestructive backup and proven recovery. | Complete (documents reviewed) |
| Re-run all C++/ARM/Windows validation locally | This Windows environment has no `g++`, Clang, MSVC C++ compiler, Python interpreter, or ARM toolchain available; local rerun could not be performed. CI #162 is the current verification evidence for the identical commit. | Blocked (toolchain unavailable locally) |
| Stock-firmware MIDI route capture | Run `py tools\mpk225_usb_probe.py midi-ports`, then `monitor-all --seconds 60 --match MPK225 --json mpk225-passive-routing.json`; observe keybed input and, only if available, ordinary external DIN-MIDI input. | Hardware evidence required |
| Keybed, velocity, panel/LCD, USB and end-to-end acceptance | With stock firmware and no firmware writes, record note/velocity, button/display, USB enumeration/cable mapping, DIN, note-release and reconnect observations using project acceptance docs. Firmware-specific raw keybed behavior remains unverified. | Hardware evidence required |
| Nondestructive backup and independent recovery proof | Establish device identity and readout protection read-only; if locked, stop. Only accept two identical full-flash and option-byte reads plus independently demonstrated restoration on a nonessential unit. | Hardware evidence required |
| Installation/release readiness | Requires all hardware tasks above to pass. No flashable image, updater SysEx, option-byte change, or installation recommendation is authorized by this tracker. | Explicitly not ready |

## Completion boundary

All software tasks accepted by the latest available CI for this branch head are complete. The local toolchain gap limits independent reproduction, not the recorded CI result. Remaining tasks need physical MPK225 evidence; do not infer them from a successful compile, tests, schematics, or a Windows host audition. Current branch head remains offline-verified only and is not safe to install.

