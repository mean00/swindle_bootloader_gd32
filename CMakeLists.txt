#-----------------------------------------------------------------------------
# Force PA13 to ground to force DFU
#-----------------------------------------------------------------------------
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
SET(CMAKE_EXPORT_COMPILE_COMMANDS ON)

cmake_minimum_required(VERSION 3.13)
MESSAGE(STATUS "=====================")
MESSAGE(STATUS "|Starting BootLoader|")
MESSAGE(STATUS "=====================")
#
find_package(Python3 COMPONENTS Interpreter)

IF(Python3_FOUND)
  message(STATUS "Python3 found: ${Python3_VERSION}")
ELSE()
  MESSAGE(FATAL_ERROR "Python3 not found")
ENDIF()
#____________
SET(AF_FOLDER  ${CMAKE_SOURCE_DIR}/)
# Not needed flash size is auto detected OPTION(USE_GD32F3 "Build for USE_GD32F303" OFF)
SET(LN_CUSTOM_LD_SCRIPT ${CMAKE_BINARY_DIR}/linker_script.ld)
include(./mcuSelect.cmake)

SET(USE_CLANG True CACHE INTERNAL "")
#SET(LN_LTO "-flto" CACHE INTERNAL "")
OPTION(USE_GD32_CRYSTALLESS False "Use GD32F303 crystallless mode")

IF(USE_GD32_CRYSTALLESS)
  ADD_DEFINITIONS("-DUSE_GD32_CRYSTALLESS")
ENDIF(USE_GD32_CRYSTALLESS)

ADD_DEFINITIONS(" -fno-builtin-memcpy ")
ADD_DEFINITIONS("-DENABLE_CHECKSUM")

ADD_DEFINITIONS("-DXXH_NO_STDLIB  -DXXH_NO_XXH3 -DXXH_NO_LONG_LONG -DXXH_NO_STREAM")

ADD_DEFINITIONS("-DLN_MCU_SPEED=${LN_MCU_SPEED}")
ADD_DEFINITIONS("-Os -g3")

SET(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/toolchain_arm_gd32fx_clang.cmake CACHE INTERNAL "")
SET(CMAKE_C_COMPILER_FORCED TRUE CACHE INTERNAL "")
SET(CMAKE_CXX_COMPILER_FORCED TRUE CACHE INTERNAL "")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_SOURCE_DIR}/cmake CACHE INTERNAL "")

#____________
SET(BOOTLOADER_SIZE 8)
SET(APP_SIZE  240)
math(EXPR APP_START    "0x8000000" OUTPUT_FORMAT HEXADECIMAL )
math(EXPR PAYLOAD_SIZE "${LN_MCU_FLASH_SIZE}-${BOOTLOADER_SIZE}-${LN_MCU_EEPROM_SIZE}")

configure_file(flash_config.h.in flash_config.h @ONLY)
configure_file(${CMAKE_SOURCE_DIR}/clang.lld.in ${CMAKE_BINARY_DIR}/linker_script.ld  @ONLY)
PROJECT(lnDSOBootloader NONE) # the NONE is very important !
enable_language(C CXX ASM) # this is important too!
#
include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_BINARY_DIR})

ADD_DEFINITIONS("-DVERSION=\"0.0\"")
SET(LN_EXT "arm_gd32fx" CACHE INTERNAL "")

include(lnCmake)
include_directories(src)

include_directories(lnArduino_mini/include)

# LCD driver
#_____________________________________________________

configure_file("${CMAKE_SOURCE_DIR}/src/memory_config.h.in" "${CMAKE_BINARY_DIR}/memory_config.h" @ONLY)
add_library(miniLnArduino STATIC lnArduino_mini/systemHelper.cpp
    lnArduino_mini/lnGPIO.cpp
    lnArduino_mini/systemAlloc.cpp
    lnArduino_mini/lnRCU.cpp
    lnArduino_mini/lnCpuID_arm_gd32fx.cpp
    lnArduino_mini/lnIRQ_arm_gd32fx.cpp)


GENERATE_GD32_FIRMWARE(swindle_gd_bootloader
                src/main.cpp
                src/usb.cpp
                # src/xxhash.c
                src/ch32_crc.c
                src/hash_check.cpp
                arm_gd32fx/start.cpp
                src/stubs.cpp
                src/dfu.cpp
                src/reboot.cpp
                )


target_link_libraries(swindle_gd_bootloader  miniLnArduino)
#
#SET(SCRIPT ${LN_CUSTOM_LD_SCRIPT} CACHE INTERNAL "")

#TARGET_LINK_OPTIONS(swindle_gd_bootloader PRIVATE "-T${SCRIPT}")

#
#

MESSAGE(STATUS "Configured for ${LN_MCU}")
MESSAGE(STATUS "Total Flash ${LN_MCU_FLASH_SIZE}")

IF(USE_GD32_CRYSTALLESS)
  set(EXTRA_NAME "_nocrystal")
ENDIF()

set_target_properties(swindle_gd_bootloader PROPERTIES OUTPUT_NAME swindle_gd_bootloader_arm${EXTRA_NAME})

#IF(USE_GD32F3)
#set_target_properties(swindle_gd_bootloader PROPERTIES OUTPUT_NAME swindle_gd_bootloader_GD32F303)
#ELSE(USE_GD32F3)
#set_target_properties(swindle_gd_bootloader PROPERTIES OUTPUT_NAME swindle_gd_bootloader_STM32F103)
#ENDIF(USE_GD32F3)

