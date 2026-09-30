# Changelog

All notable changes to this project are documented in this file.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses [Semantic Versioning](https://semver.org/).

## [3.0.0] - 2026-10-01

A complete rewrite of the firmware with a hand-written LVGL 9 user interface.

### Added

- Four watch faces (Digital, Analog, Modular, Minimal) with accent colours and an always-on display.
- App launcher, quick settings panel, notification center and tiles next to the watch face.
- Apps: Activity, Workout, Sleep, Alarm, Timer, Stopwatch, Weather, Music, Recorder, Calendar and Settings.
- Phone link through Gadgetbridge (Bangle.js protocol): notifications, calls, music, weather, calendar, find my phone, time sync.
- Raise to wake and lower to sleep, tap to wake, bedtime mode, battery saver.
- Weather from Open-Meteo with automatic location or city search.
- Extras: WLED light control with mDNS discovery, clap control, car control over WebSocket.
- Wireless firmware update from a web browser.
- Motion sensor calibration, FPS monitor.
- Font and image generators in `tools/`.

### Changed

- Layered source structure in `Smartwatch/src` (core, drivers, algorithms, services, ui, assets).
- Rendering tuned for the ESP32-S3: internal DMA draw buffers, LVGL hot paths in IRAM, snapshot page transitions, pre-rendered app launcher.

### Removed

- The SquareLine Studio based user interface.

## [2.x and earlier]

- First versions with a SquareLine Studio user interface.
