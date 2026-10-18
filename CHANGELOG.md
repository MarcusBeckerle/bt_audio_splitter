# Changelog

## 0.1.7 — 2026-10-18 — Prerelease

- Give each frequency display its own buffered child window. Meter updates no
  longer invalidate the main surface containing the setup section and controls.
- Buffer every owner-drawn button independently, including the setup heading,
  and suppress separate background erasure. This also covers direct drawing
  outside WM_PAINT. Only change enabled state when it actually changes.
- Remove the whole-window WS_EX_COMPOSITED workaround from 0.1.6; retain batched
  layout and buffered parent, sliders, buttons and graphs.
- Add a real message-loop regression with changing spectra: compare static
  control pixels after intermediate messages and require zero parent/button
  redraws. Keep the 630 scroll, drag, scaling and state image comparisons.

## 0.1.6 — 2026-10-18 — Prerelease

- Fix flicker across sliders, pause/menu buttons and setup: compose the parent
  and child controls together, batch their positions and finish each scroll frame.
- Spectrum ticks invalidate only their graph regions. Unchanged controls and
  layouts are left alone; off-screen spectra are skipped during painting.
- Cache the twelve glowing LED-column shapes once per size/DPI change and reuse
  them across all six graphs, avoiding repeated gradient rendering while scrolling.
- Extend regression checks to immediate scroll frames, all controls during idle
  and meter updates, and wheel bursts with five active spectra.
- Try IAudioClient3 with a driver-aligned period of at least 5 ms when the device
  mix format exactly matches stereo float input. Retain high-quality resampling
  and legacy minimum-buffer/20-ms fallback for other formats and drivers.
- Audio self-test reports actual output buffer sizes and engine period. Smaller
  buffers depend on the driver; this does not remove Bluetooth transport latency.

## 0.1.5 — 2026-10-18 — Prerelease

- Floating spectrum LEDs with a subtle glow and a smooth green/yellow/orange/red
  palette, without a dark panel or inactive grid.
- Speaker icon on Mute all / Unmute all. Each output's speaker is now a keyboard-
  accessible mute button that preserves its slider volume and connection.
- Persist individual mute independently of master mute; unmuting all preserves
  individually muted outputs. Settings versions 1 and 2 migrate to version 3.
- Test device mute persistence, effective audio gain, settings migration and
  repainting the new buttons: 25 core tests and 522 UI image comparisons.

## 0.1.4 — 2026-10-18 — Prerelease

- Fix volume-thumb trails by invalidating the full custom-painted slider before
  native painting. Avoid repositioning controls when their bounds did not change.
- Run UI tests with the application's actual Common Controls v6 manifest;
  simulate dragging during refresh and changing spectra, plus mute/unmute.
- Show the running version in the window title and explain when a different
  already-running version prevented a newly downloaded EXE from starting.
- Add segmented green/yellow/orange frequency displays to Virtual Input and
  below each output's volume slider. 100% matches the input, 0%/mute is flat.
- Add Mute all / Unmute all while retaining each output's volume, pause and delay.
- Logarithmic volume taper (-60..0 dB, plus digital mute) for finer quiet control.
- Event-driven WASAPI capture/render; request 20 ms output buffers instead of
  80 ms and bound each live queue to 40 ms instead of 180 ms.
- Add persistent manual per-output delay (0–1000 ms) to align faster outputs with
  slower headphones. Version 1 settings migrate automatically to version 2.

Bluetooth transport latency is not removed, hardware clocks are not locked,
and lip-sync may still require an audio offset in the movie player. The output
spectra preview software gain using the same input frame; they are not acoustic
measurements and do not visualize the manual delay's time shift.

## 0.1.3 — 2026-10-18 — Prerelease

- Fix duplicated text and broken buttons after scrolling: move native controls
  without copying old pixels, then invalidate both the parent and every child.
- Add 234 real-window repaint regression comparisons for 0/2/5 outputs,
  100/150/200% layout scaling, window resizing, scrolling, setup expansion,
  keyboard focus and periodic refresh. CI and release builds run these checks.
- Document VB-CABLE Windows 11 compatibility despite WIN10 labels in its setup.

## 0.1.2 — 2026-10-18 — Prerelease

- Fix unattended signature verification: temporarily trust the pinned certificate
  in the disposable runner's machine store to avoid a CurrentUser trust dialog.
- Add signing progress messages and bounded job/step timeouts.
- Update GitHub Actions to current releases, pinned to exact commits.
- First fully verified signed release; 0.1.1 stopped during runner trust setup
  and did not publish downloadable binaries.

## 0.1.1 — 2026-10-18 — Prerelease

- Self-signed Authenticode releases for Marcus Beckerle, with RFC 3161 timestamps.
- Automatic build, tests, signature verification and asset upload when a GitHub
  release is published; signing secrets isolated in the release-signing environment.
- Version metadata and filenames generated from the release tag or VERSION file.
- Public certificate and signing documentation included in signed releases.

The self-signed certificate does not remove SmartScreen warnings or establish
public publisher trust. No end-user trust-store changes are made.

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
