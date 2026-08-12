cmake_minimum_required(VERSION 3.21)

project(RunBayClient VERSION 0.1.0 LANGUAGES CXX)

include(FetchContent)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

foreach(CONFIG_TYPE DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_TYPE} "${CMAKE_BINARY_DIR}/bin")
endforeach()

set(CMAKE_DISABLE_FIND_PACKAGE_WrapVulkanHeaders ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

if(WIN32)
    set(RUNBAY_SERVICE_PLATFORM_SOURCE src/ServiceManagerPlatform_windows.cpp)
    set(RUNBAY_SERVICE_WINDOW_PLATFORM_SOURCE src/ServiceManagerWindow_windows.cpp)
elseif(APPLE)
    set(RUNBAY_SERVICE_PLATFORM_SOURCE src/ServiceManagerPlatform_macos.cpp)
    set(RUNBAY_SERVICE_WINDOW_PLATFORM_SOURCE src/ServiceManagerWindow_macos.cpp)
else()
    set(RUNBAY_SERVICE_PLATFORM_SOURCE src/ServiceManagerPlatform_other.cpp)
    set(RUNBAY_SERVICE_WINDOW_PLATFORM_SOURCE src/ServiceManagerWindow_nonwindows.cpp)
endif()

set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
find_package(yaml-cpp CONFIG QUIET)
if(NOT TARGET yaml-cpp::yaml-cpp)
    FetchContent_Declare(
        yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG yaml-cpp-0.9.0
    )
    FetchContent_MakeAvailable(yaml-cpp)
endif()

if(MSVC AND TARGET yaml-cpp)
    target_compile_options(yaml-cpp PRIVATE /wd4267)
endif()

qt_add_executable(runbay-client
    src/main.cpp
    src/AppUtils.cpp
    src/AppUtils.h
    src/ApiClient.cpp
    src/ApiClient.h
    src/MainWindow.cpp
    src/MainWindow.h
    src/ServiceManagerWindow.cpp
    src/ServiceManagerWindow.h
    ${RUNBAY_SERVICE_WINDOW_PLATFORM_SOURCE}
    src/ServiceManagerPlatform.h
    ${RUNBAY_SERVICE_PLATFORM_SOURCE}
    src/ServiceRuntimeConfig.cpp
    src/ServiceRuntimeConfig.h
    src/Task.h
    src/TaskTableModel.cpp
    src/TaskTableModel.h
    resources/runbay.qrc
)

target_link_libraries(runbay-client PRIVATE Qt6::Widgets Qt6::Network yaml-cpp::yaml-cpp)

if(APPLE)
    set(RUNBAY_MACOS_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/runbay-app-icon.icns")
    target_sources(runbay-client PRIVATE "${RUNBAY_MACOS_ICON}")
    set_source_files_properties("${RUNBAY_MACOS_ICON}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
    set_target_properties(runbay-client PROPERTIES
        OUTPUT_NAME "RunBay"
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "RunBay"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.runbay.client"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "runbay-app-icon.icns"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/resources/macos/Info.plist.in"
    )
endif()

if(MSVC)
    target_compile_options(runbay-client PRIVATE /utf-8)
endif()

if(WIN32)
    target_sources(runbay-client PRIVATE resources/runbay.rc)
    set_target_properties(runbay-client PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_libraries(runbay-client PRIVATE credui ole32 uuid)
endif()
