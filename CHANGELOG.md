# Changelog

## 0.1.0 — 2026-10-18 — Prerelease

- Native C++20 / Win32 application, portable x64 EXE smaller than 1 MB.
- Minimal interface with real Windows device names and up to five outputs.
- Separate persistent volume, connect/pause intent and removal per endpoint ID.
- WASAPI stereo loopback and shared outputs, Windows high-quality resampling.
- Independent capture/output threads, bounded queues and device recovery.
- Verified VB-CABLE download, vendor installation/removal UI and optional
  elevated endpoint rename to “Audio Splitter”.
- Pinned build toolchain, automated tests, silent native audio test and licenses.

Known limits: Bluetooth capacity and latency depend on hardware; no automatic
cross-device clock synchronization. Bluetooth listening tests and driver setup
on the target machine remain pending. See README for the acceptance checklist.
