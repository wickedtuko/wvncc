# wvncc AI Coding Agent Instructions

**wvncc** is a Qt6-based VNC client for remote desktop viewing and interaction on Windows.

## Project Architecture

### Core Components
- **Main Application** ([main.cpp](../main.cpp)): Command-line entry point requiring `<server_ip>` and `<port>` arguments
- **MainWindow** ([mainwindow.h](../mainwindow.h), [mainwindow.cpp](../mainwindow.cpp)): Qt QMainWindow that manages the VNC connection and display
- **UI Definition** ([mainwindow.ui](../mainwindow.ui)): Qt Designer file (generated UI components)

### Integration Pattern
wvncc wraps **LibVNCServer** (actually LibVNCClient - RFB protocol library) with a Qt GUI:
- Uses C library directly via `extern "C" { #include "rfb/rfbclient.h" }`
- Forward-declares `rfbClient` struct in header to avoid exposing C headers
- Thread-based architecture: VNC message loop runs in background thread (`m_vncThread`) while Qt UI thread handles rendering

### Data Flow
1. **Connection** → `connectToServer()` creates `rfbClient`, configures RGB16 pixel format
2. **Async Updates** → `WaitForMessage()` + `HandleRFBServerMessage()` poll server (500ms timeout)
3. **Framebuffer Update Callback** → `framebufferUpdateCallback` (static) → `handleFramebufferUpdate()` queues repaint
4. **Rendering** → `paintEvent()` draws `QImage` to window
5. **Input** → Mouse events scale to remote resolution and send via `SendPointerEvent()`

## Build Workflow

**Prerequisites**: Qt 6.10.1 MinGW 64-bit, LibVNCServer 0.9.15 installed to `C:/libvnc-install`

**Key Build Commands** (Windows-specific):
```bash
# Setup environment
call c:\Qt\6.10.1\mingw_64\bin\qtenv2.bat
set path=C:\Qt\Tools\CMake_64\bin;%path%

# Configure & build
cd c:\g\wvncc\build
cmake .. -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH="C:/libvnc-install"
mingw32-make

# Run with arguments
.\wvncc.exe 192.168.1.100 5900
```

**CMake Key Settings** ([CMakeLists.txt](../CMakeLists.txt)):
- `CMAKE_CXX_STANDARD 17` - C++17 required
- `QT_VERSION_MAJOR` - Supports both Qt5/Qt6, detects at configure time
- `find_package(LibVNCServer)` - Optional; warns if not found but doesn't fail (enables conditional linking)

## Project-Specific Patterns

### C/C++ Integration (Critical)
- **Header isolation**: C library headers only included in `.cpp` files (mainwindow.cpp)
- **Callback pattern**: Uses `rfbClientGetClientData`/`rfbClientSetClientData` to access `this` pointer in static callback
  - See `framebufferUpdateCallback()` → passes client data to instance method
- **String allocation**: VNC client strings (e.g., `m_client->serverHost`) must be allocated with `strdup()`

### Qt/Threading Pattern
- **Single-threaded UI**: All Qt drawing/events on main thread
- **Background VNC thread**: Spawned in `connectToServer()`, polls `WaitForMessage()` in loop
- **Thread-safe updates**: UI changes triggered by `update()` (queued signal), not direct draw calls
- **Clean shutdown**: `~MainWindow()` sets `m_connected=false`, joins thread before deletion

### VNC Configuration ([mainwindow.cpp](../mainwindow.cpp#L51-L70))
- **Pixel format**: RGB16 (5-6-5 bits) - hardcoded for performance
- **Compression**: Level 9 + tight/ultra encodings
- **Remote cursor**: Enabled
- **Connection timeout**: 500ms polling interval (prevent blocking on network issues)

### Build Behavior
- `CMAKE_AUTOUIC`, `CMAKE_AUTOMOC`, `CMAKE_AUTORCC` enabled → Qt auto-generates code from `.ui`/`.h`
- No explicit `qt_finalize_executable()` if Qt 5 → only for Qt 6

## Common Tasks

### Modifying the UI
1. Edit [mainwindow.ui](../mainwindow.ui) in Qt Designer or text editor
2. Qt's `AUTOMOC` auto-generates `ui_mainwindow.h`
3. Access widgets via `ui->widgetName`

### Adding VNC Features
- Check [libvncserver/include/rfb/rfbclient.h](../../libvncserver/include/rfb/rfbclient.h) for available functions
- Keyboard input: Use `SendKeyEvent(client, key, down)`
- Clipboard: `SendClientCutText()`
- Update framebuffer format in `connectToServer()` if changing pixel depth

### Troubleshooting Build Failures
- **LibVNCServer not found**: Check `CMAKE_PREFIX_PATH` points to `C:/libvnc-install/lib/cmake/LibVNCServer`
- **Missing DLLs at runtime**: Ensure Qt bin and `C:/libvnc-install/bin` are in PATH
- **Qt version mismatch**: Verify CMakeLists.txt matches installed Qt version

## External Dependencies
- **Qt 6.10.1** (or Qt 5): GUI framework with CMake integration
- **LibVNCServer 0.9.15**: RFB protocol (both server/client); wvncc uses client (`vncclient` library)
- **MinGW 64-bit**: C++ compiler (C17 standard)

## File Organization
```
wvncc/
  main.cpp                    # Entry point, arg parsing
  mainwindow.h/cpp/ui         # Main UI logic, VNC integration
  CMakeLists.txt              # Build configuration
  BUILD.md / setup.md         # Build instructions (Windows-specific)
  build/                      # Generated build artifacts
```
