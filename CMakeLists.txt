cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(ABSvita C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

# Release builds pass the tag: cmake -DAPP_VERSION=1.2.3 (CI does this)
set(APP_VERSION "0.1.1" CACHE STRING "app version")
set(VITA_APP_NAME "ABSvita")
set(VITA_TITLEID "ABSV00001")
# param.sfo VERSION is "MM.mp": 0.1.0 -> 00.10, 1.2.3 -> 01.23 (minor and patch kept below 10)
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.?([0-9]*)" _v "${APP_VERSION}")
set(_major "${CMAKE_MATCH_1}")
set(_minor "${CMAKE_MATCH_2}")
set(_patch "${CMAKE_MATCH_3}")
if(_patch STREQUAL "")
  set(_patch 0)
endif()
if(_minor GREATER 9)
  set(_minor 9)
endif()
if(_patch GREATER 9)
  set(_patch 9)
endif()
if(_major LESS 10)
  set(_major "0${_major}")
endif()
set(VITA_VERSION "${_major}.${_minor}${_patch}")
set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d PARENTAL_LEVEL=1")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -Wall -Wno-unused-parameter -Wno-sign-compare -std=gnu11")

file(GLOB SOURCES
  src/core/*.c src/net/*.c src/ui/*.c src/vita/*.c
  vendor/cjson/cJSON.c vendor/sonic/sonic.c vendor/helix-aac/*.c)

add_executable(${PROJECT_NAME} ${SOURCES})
target_compile_definitions(${PROJECT_NAME} PRIVATE APP_VERSION="${APP_VERSION}")
# Test builds for the emulator: cmake -DAUTOPILOT=ON (never in releases)
option(AUTOPILOT "scripted input from ux0:data/ABSvita/autopilot.txt" OFF)
if(AUTOPILOT)
  target_compile_definitions(${PROJECT_NAME} PRIVATE ABSVITA_AUTOPILOT)
endif()
target_include_directories(${PROJECT_NAME} PRIVATE
  /opt/curl-mbed/include
  src/core src/net src/ui src/vita vendor/cjson vendor/sonic vendor/helix-aac vendor/stb)

target_link_libraries(${PROJECT_NAME}
  vita2d freetype png jpeg bz2 z
  /opt/curl-mbed/lib/libcurl.a mbedtls mbedx509 mbedcrypto
  mpg123
  m pthread
  SceDisplay_stub SceGxm_stub SceCtrl_stub SceTouch_stub SceAudio_stub
  ScePower_stub SceSysmodule_stub SceCommonDialog_stub SceAppUtil_stub SceAppMgr_stub
  SceNet_stub SceNetCtl_stub SceHttp_stub SceSsl_stub SceRtc_stub SceIme_stub
  SceKernelThreadMgr_stub SceKernelModulemgr_stub SceIofilemgr_stub SceProcessmgr_stub SceLibKernel_stub SceSysmem_stub
)

vita_create_self(eboot.bin ${PROJECT_NAME} UNSAFE)
vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE sce_sys/icon0.png sce_sys/icon0.png
  FILE sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
  FILE sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  FILE sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
  FILE assets/fonts/Manrope-500.ttf assets/Manrope-500.ttf
  FILE assets/fonts/Manrope-700.ttf assets/Manrope-700.ttf
)
