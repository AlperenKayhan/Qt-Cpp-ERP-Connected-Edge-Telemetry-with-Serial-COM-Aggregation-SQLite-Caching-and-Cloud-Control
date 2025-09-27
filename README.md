# ERP Edge Gateway (Qt/C++): Threaded COM Telemetry + SQLite + WebSocket Control

A production-style Qt/C++ edge client that ingests newline-delimited sensor data from ESP32 devices over serial (COM), persists events to a local SQLite DB for offline resilience, and synchronizes with an IRP backend over a secure WebSocket. Designed for the Firefly ROC-RK3566-PC, but runs on Windows and Linux.

## ✨ Features
- **Threaded serial I/O:** one worker thread per selected COM port; safe open/close and fast unplug detection.
- **Hot-plug rescan:** “Reboot” re-enumerates ports without restarting the app.
- **Simulation mode:** generate plausible readings without hardware.
- **SQLite caching:** offline-first, table `warnings(timestamp, level, distance, xn)`.
- **WebSocket control:** heartbeat (ping/pong), `send_logs`, `get_d_parameters`, `refresh`, `reboot`.
- **Operator UI:** port dropdown (Select/Simulation/All/Specific), Start/Stop, Reset DB, Send Logs, Get Parameters, 3D scatter, live logs.

## 🏗️ Architecture (modules)
- `ComThread` — `QThread` worker that **owns** a `QSerialPort`, blocks on `waitForReadyRead(100ms)`, parses lines on `\n`, emits `distanceReceived(float)`, stops on `errorOccurred` (unplug).
- `ComPortManager` — starts/stops workers based on mode (Idle / Simulation / Single / All), tracks open ports, auto-disables reading when all ports close.
- `DvClient` — HTTPS session bootstrap, secure `QWebSocket` to IRP, heartbeat loop, command handlers, SQLite insert/upload pipeline.
- `MainWindow` — operator UI; port selection; buttons; table bound to SQLite; scatter plot; log console.

**Serial protocol:** each frame is an ASCII float in centimeters, terminated by newline, e.g. `0.37\n`.  
**Stale-data guard:** if the last serial value is “old” when a heartbeat arrives, the insert is skipped (unless in Simulation).

---

## 🔧 Build

### Prerequisites
- **Qt 6.x** (tested with 6.9.x) with QtWidgets, QtSerialPort, QtNetwork, QtWebSockets, QtSql
- **CMake 3.21+**


