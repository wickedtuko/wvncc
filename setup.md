call c:\Qt\6.10.1\mingw_64\bin\qtenv2.bat
set path=C:\Qt\Tools\CMake_64\bin;%path%

mkdir c:\g
cd c:\g
git clone https://github.com/LibVNC/libvncserver.git
cd libvncserver
git checkout LibVNCServer-0.9.15
mkdir build && cd build
cmake .. -G "MinGW Makefiles" -DCMAKE_INSTALL_PREFIX="C:/libvnc-install"
mingw32-make
mingw32-make install
