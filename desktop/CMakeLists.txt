cmake_minimum_required(VERSION 3.16)
project(HNN_CN_ET_Desktop VERSION 1.0.5 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC OFF)
set(CMAKE_AUTOUIC OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(QT_ROOT "$ENV{QT_ROOT}")
if(NOT QT_ROOT)
  set(QT_ROOT "/opt/qt/5.15.2/mingw81_64")
endif()

set(CMAKE_PREFIX_PATH ${QT_ROOT})
set(CMAKE_INCLUDE_PATH "${QT_ROOT}/include")
set(CMAKE_LIBRARY_PATH "${QT_ROOT}/lib")

find_package(Qt5 REQUIRED COMPONENTS Widgets Network)

# Embed app icon via Qt resource

set(SOURCES
  src/qrc_resources.cpp
  src/main.cpp
  src/MainWindow.cpp
  src/SettingsDialog.cpp
  src/ApiClient.cpp
  src/McpClient.cpp
  src/AgentEngine.cpp
  src/ModelManager.cpp
  src/ApiService.cpp
  src/Updater.cpp
  src/ThemeManager.cpp
  src/HistoryStore.cpp
  src/ChatView.cpp
)

set(HEADERS
  src/MainWindow.h
  src/SettingsDialog.h
  src/ApiClient.h
  src/McpClient.h
  src/AgentEngine.h
  src/ModelManager.h
  src/ApiService.h
  src/Updater.h
  src/ThemeManager.h
  src/HistoryStore.h
  src/ChatView.h
  src/Common.h
)

add_executable(HNN_CN_ET_Desktop WIN32 ${SOURCES} ${HEADERS})

target_link_libraries(HNN_CN_ET_Desktop PRIVATE Qt5::Widgets Qt5::Network)

target_include_directories(HNN_CN_ET_Desktop PRIVATE src ${QT_ROOT}/include)

# Version info resource (copyright ET, Win11-style metadata)
set(VERSION_RC ${CMAKE_CURRENT_SOURCE_DIR}/packaging/versioninfo.rc)
if(EXISTS ${VERSION_RC})
  target_sources(HNN_CN_ET_Desktop PRIVATE ${VERSION_RC})
endif()

# Icon resource
set(APP_ICON ${CMAKE_CURRENT_SOURCE_DIR}/resources/app.rc)
if(EXISTS ${APP_ICON})
  target_sources(HNN_CN_ET_Desktop PRIVATE ${APP_ICON})
endif()

if(MINGW)
  target_compile_options(HNN_CN_ET_Desktop PRIVATE -O2 -Wall -Wextra)
  target_link_libraries(HNN_CN_ET_Desktop PRIVATE -static-libgcc -static-libstdc++)
endif()
