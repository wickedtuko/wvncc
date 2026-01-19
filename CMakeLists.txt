cmake_minimum_required(VERSION 3.16)

project(wvncc VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

# Find LibVNCServer for VNC client functionality
# If using custom install location, pass -DCMAKE_PREFIX_PATH="C:/libvnc-install" to cmake
find_package(LibVNCServer)
if(NOT LibVNCServer_FOUND)
    message(WARNING "LibVNCServer not found. Specify CMAKE_PREFIX_PATH or install libvncserver.")
    message(WARNING "Example: cmake .. -G \"MinGW Makefiles\" -DCMAKE_PREFIX_PATH=\"C:/libvnc-install\"")
endif()

set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        mainwindow.ui
        resources.qrc
)

# Add Windows resource file for version info
if(WIN32)
    list(APPEND PROJECT_SOURCES wvncc.rc)
endif()

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(wvncc
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET wvncc APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(wvncc SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(wvncc
            ${PROJECT_SOURCES}
        )
    endif()
endif()

target_link_libraries(wvncc PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

if(LibVNCServer_FOUND)
    target_link_libraries(wvncc PRIVATE LibVNCServer::vncclient)
    target_include_directories(wvncc PRIVATE ${LIBVNCSERVER_INCLUDE_DIR})
else()
    # Fallback: manually link LibVNCServer if CMake package not found
    target_include_directories(wvncc PRIVATE C:/libvnc-install/include)
    target_link_directories(wvncc PRIVATE C:/libvnc-install/lib)
    target_link_libraries(wvncc PRIVATE vncclient ws2_32 iphlpapi)
endif()

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.wvncc)
endif()
set_target_properties(wvncc PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS wvncc
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(wvncc)
endif()
