# Building wvncc with MinGW

## Prerequisites

1. Qt 6.10.1 with MinGW 64-bit installed
2. LibVNCServer built and installed (see below)

## Step 1: Build and Install LibVNCServer

Follow the instructions in [setup.md](setup.md) to build libvncserver, or:

```cmd
call c:\Qt\6.10.1\mingw_64\bin\qtenv2.bat
set path=C:\Qt\Tools\CMake_64\bin;%path%

cd c:\g\libvncserver
mkdir build
cd build
cmake .. -G "MinGW Makefiles" -DCMAKE_INSTALL_PREFIX="C:/libvnc-install"
mingw32-make
mingw32-make install
```

## Step 2: Build wvncc

```cmd
call c:\Qt\6.10.1\mingw_64\bin\qtenv2.bat
set path=C:\Qt\Tools\CMake_64\bin;%path%

cd c:\g\wvncc
mkdir build
cd build
cmake .. -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH="C:/libvnc-install"
mingw32-make
```

## Step 3: Run wvncc

```cmd
cd c:\g\wvncc\build
.\wvncc.exe <server_ip> <port>
```

Example:
```cmd
.\wvncc.exe 192.168.1.100 5900
```

## Troubleshooting

**LibVNCServer not found:**
- Ensure libvncserver is installed to `C:/libvnc-install`
- Verify `CMAKE_PREFIX_PATH` points to the install directory
- Check that `C:/libvnc-install/lib/cmake/LibVNCServer` exists

**Missing DLLs at runtime:**
- Ensure Qt's bin directory is in PATH (qtenv2.bat does this)
- LibVNCServer DLLs should be in `C:/libvnc-install/bin`
- You may need to copy DLLs or add to PATH: `set path=C:/libvnc-install/bin;%path%`
