cmake_minimum_required(VERSION 3.16)

set(MOLTBOOKMONITOR_VERSION_DEFAULT "0.1.6")
set(MOLTBOOKMONITOR_VERSION "${MOLTBOOKMONITOR_VERSION_DEFAULT}")
set(MOLTBOOKMONITOR_VERSION_OVERRIDE "" CACHE STRING "Override application semantic version (for CI/release builds)")
mark_as_advanced(MOLTBOOKMONITOR_VERSION_OVERRIDE)

# Backward compatibility for legacy -DMOLTBOOKMONITOR_VERSION usage.
# Ignore stale cache value from historical default 0.1.0 to avoid version drift.
if(MOLTBOOKMONITOR_VERSION_OVERRIDE)
    set(MOLTBOOKMONITOR_VERSION "${MOLTBOOKMONITOR_VERSION_OVERRIDE}")
elseif(DEFINED CACHE{MOLTBOOKMONITOR_VERSION})
    set(_legacy_cached_version "$CACHE{MOLTBOOKMONITOR_VERSION}")
    if(NOT _legacy_cached_version STREQUAL "" AND NOT _legacy_cached_version STREQUAL "0.1.0")
        set(MOLTBOOKMONITOR_VERSION "${_legacy_cached_version}")
        message(WARNING "Using deprecated cache variable MOLTBOOKMONITOR_VERSION. Prefer MOLTBOOKMONITOR_VERSION_OVERRIDE.")
    endif()
endif()

set(MOLTBOOKMONITOR_BUNDLE_ID "com.moltbook.monitor" CACHE STRING "Bundle identifier")
option(MOLTBOOKMONITOR_MACOS_ADHOC_CODESIGN "Ad-hoc sign macOS app bundle after deployment" ON)

if(NOT MOLTBOOKMONITOR_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+([-.][0-9A-Za-z.-]+)?$")
    message(FATAL_ERROR "MOLTBOOKMONITOR_VERSION must be semantic versioning-compatible, got: ${MOLTBOOKMONITOR_VERSION}")
endif()

project(MoltbookMonitor VERSION ${MOLTBOOKMONITOR_VERSION} LANGUAGES CXX)

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
    src/app/main.cpp
    src/monitor/monitorcontroller.h
    src/monitor/monitorcontroller_model.cpp
    src/monitor/monitorcontroller_agents.cpp
    src/monitor/monitorcontroller_profile.cpp
    src/monitor/monitorcontroller_updates.cpp
    src/monitor/monitorcontroller_state.cpp
)

set(MOLTBOOKMONITOR_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${MOLTBOOKMONITOR_GENERATED_DIR}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AppVersion.h.in"
    "${MOLTBOOKMONITOR_GENERATED_DIR}/app_version.h"
    @ONLY
)

target_include_directories(appMoltbookMonitor
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${MOLTBOOKMONITOR_GENERATED_DIR}
)

qt_add_qml_module(appMoltbookMonitor
    URI MoltbookMonitor
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/components/AgentCard.qml
        qml/components/AgentListPanel.qml
        qml/components/MonitorControlPanel.qml
        qml/components/NotificationBanner.qml
        qml/components/NotificationsPanel.qml
        qml/components/RequestLogPanel.qml
        qml/components/StatusMessagePanel.qml
    RESOURCES
        qml/js/UiHelpers.js
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(appMoltbookMonitor PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER ${MOLTBOOKMONITOR_BUNDLE_ID}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
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

qt_generate_deploy_qml_app_script(
    TARGET appMoltbookMonitor
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})

if(APPLE AND MOLTBOOKMONITOR_MACOS_ADHOC_CODESIGN)
    # Qt deployment can copy and patch binaries/plugins; finalize with one ad-hoc re-sign pass.
    install(CODE "
        set(app_bundle \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/appMoltbookMonitor.app\")
        if(NOT EXISTS \"\${app_bundle}\")
            message(FATAL_ERROR \"Expected app bundle not found: \${app_bundle}\")
        endif()

        execute_process(
            COMMAND xattr -cr \"\${app_bundle}\"
            RESULT_VARIABLE xattr_result
            OUTPUT_QUIET
            ERROR_QUIET
        )
        if(NOT xattr_result EQUAL 0)
            message(WARNING \"xattr -cr failed for \${app_bundle}; continuing with codesign.\")
        endif()

        execute_process(
            COMMAND codesign --force --deep --sign - --timestamp=none \"\${app_bundle}\"
            RESULT_VARIABLE codesign_result
        )
        if(NOT codesign_result EQUAL 0)
            message(FATAL_ERROR \"Ad-hoc codesign failed for \${app_bundle}\")
        endif()

        execute_process(
            COMMAND codesign --verify --deep --strict \"\${app_bundle}\"
            RESULT_VARIABLE verify_result
        )
        if(NOT verify_result EQUAL 0)
            message(FATAL_ERROR \"Code signature verification failed for \${app_bundle}\")
        endif()
    ")
endif()
