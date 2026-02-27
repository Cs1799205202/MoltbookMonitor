cmake_minimum_required(VERSION 3.16)

project(MoltbookMonitor VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# find_program(SCCACHE sccache REQUIRED)

# set(CMAKE_C_COMPILER_LAUNCHER ${SCCACHE})
# set(CMAKE_CXX_COMPILER_LAUNCHER ${SCCACHE})
# set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
# cmake_policy(SET CMP0141 NEW)

find_package(Qt6 6.10 REQUIRED COMPONENTS Quick Network Widgets)

qt_standard_project_setup(REQUIRES 6.10)

qt_add_executable(appMoltbookMonitor
    main.cpp
    monitorcontroller.cpp
    monitorcontroller.h
)

qt_add_qml_module(appMoltbookMonitor
    URI MoltbookMonitor
    VERSION 1.0
    QML_FILES
        Main.qml
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(appMoltbookMonitor PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appMoltbookMonitor
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appMoltbookMonitor
    PRIVATE Qt6::Quick Qt6::Network Qt6::Widgets
)

include(GNUInstallDirs)
install(TARGETS appMoltbookMonitor
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
