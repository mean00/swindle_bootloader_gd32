#-----------------------------------------------------------------------------
# Force PA13 to ground to force DFU
#-----------------------------------------------------------------------------
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

cmake_minimum_required(VERSION 3.13)
message(STATUS "=====================")
message(STATUS "|Starting BootLoader|")
message(STATUS "=====================")
#
find_package(Python3 COMPONENTS Interpreter)

if(Python3_FOUND)
  message(STATUS "Python3 found: ${Python3_VERSION}")
else()
  message(FATAL_ERROR "Python3 not found")
endif()
#____________
set(AF_FOLDER  ${CMAKE_SOURCE_DIR}/)
# Not needed flash size is auto detected OPTION(USE_GD32F3 "Build for USE_GD32F303" OFF)
set(LN_CUSTOM_LD_SCRIPT ${CMAKE_BINARY_DIR}/linker_script.ld)
include(./mcuSelect.cmake)

set(USE_CLANG True CACHE INTERNAL "")
#set(LN_LTO "-flto" CACHE INTERNAL "")
OPTION(USE_GD32_CRYSTALLESS False "Use GD32F303 crystallless mode")

if(USE_GD32_CRYSTALLESS)
  ADD_DEFINITIONS("-DUSE_GD32_CRYSTALLESS")
endif(USE_GD32_CRYSTALLESS)

ADD_DEFINITIONS(" -fno-builtin-memcpy ")
ADD_DEFINITIONS("-DENABLE_CHECKSUM")

ADD_DEFINITIONS("-DXXH_NO_STDLIB  -DXXH_NO_XXH3 -DXXH_NO_LONG_LONG -DXXH_NO_STREAM")

ADD_DEFINITIONS("-DLN_MCU_SPEED=${LN_MCU_SPEED}")
ADD_DEFINITIONS("-Os -g3")

set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/toolchain_arm_gd32fx_clang.cmake CACHE INTERNAL "")
set(CMAKE_C_COMPILER_FORCED TRUE CACHE INTERNAL "")
set(CMAKE_CXX_COMPILER_FORCED TRUE CACHE INTERNAL "")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_SOURCE_DIR}/cmake CACHE INTERNAL "")

set(ESPRIT_ROOT ${CMAKE_SOURCE_DIR}/esprit_mini)

#____________
set(BOOTLOADER_SIZE 8)
set(APP_SIZE  240)
math(EXPR APP_START    "0x8000000" OUTPUT_FORMAT HEXADECIMAL )
math(EXPR PAYLOAD_SIZE "${LN_MCU_FLASH_SIZE}-${BOOTLOADER_SIZE}-${LN_MCU_EEPROM_SIZE}")

configure_file(flash_config.h.in flash_config.h @ONLY)
configure_file(${CMAKE_SOURCE_DIR}/clang.lld.in ${CMAKE_BINARY_DIR}/linker_script.ld  @ONLY)
PROJECT(lnDSOBootloader NONE) # the NONE is very important !
enable_language(C CXX ASM) # this is important too!
#
include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_BINARY_DIR})

ADD_DEFINITIONS("-DVERSION=\"0.1\"")
set(LN_EXT "arm_gd32fx" CACHE INTERNAL "")

include(lnCmake)
include_directories(src)

include_directories(esprit_mini/include)

# LCD driver
#_____________________________________________________

configure_file("${CMAKE_SOURCE_DIR}/src_common/memory_config.h.in" "${CMAKE_BINARY_DIR}/memory_config.h" @ONLY)
set(MINI ${CMAKE_SOURCE_DIR}/esprit_mini)
add_library(miniEsprit STATIC
    ${MINI}/systemHelper.cpp
    ${MINI}/lnGPIO.cpp
    ${MINI}/systemAlloc.cpp
    ${MINI}/lnRCU.cpp
    ${MINI}/lnCpuID_arm_gd32fx.cpp
    ${MINI}/lnIRQ_arm_gd32fx.cpp)


add_executable(swindle_gd_stage2
                arm_gd32fx/vector_table.S
                src_stage2/usb.cpp
                src_stage2/dfu.cpp
                src_common/stubs.cpp
)
target_link_libraries(swindle_gd_stage2 miniEsprit c)
target_include_directories(swindle_gd_stage2 PRIVATE src_common src_stage2 )
target_link_options(swindle_gd_stage2 PRIVATE "-T${CMAKE_SOURCE_DIR}/linker/stage2.ld")
add_custom_command(TARGET swindle_gd_stage2 POST_BUILD
                   COMMAND ${CMAKE_OBJCOPY} -Obinary $<TARGET_FILE:swindle_gd_stage2> $<TARGET_FILE:swindle_gd_stage2>.bin
                   COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/bin2h.py $<TARGET_FILE:swindle_gd_stage2>.bin > ${CMAKE_BINARY_DIR}/stage2_payload.h
                   WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                   COMMENT "Generating stage2_payload.h"
)

GENERATE_GD32_FIRMWARE(swindle_gd_bootloader
                src_stage1/main.cpp
                src_stage1/ch32_crc.c
                src_common/stubs.cpp
                arm_gd32fx/start.cpp
                )
add_dependencies(swindle_gd_bootloader swindle_gd_stage2)

target_include_directories(swindle_gd_bootloader PRIVATE src_common src_stage1 )

target_link_libraries(swindle_gd_bootloader  miniEsprit)
#
#set(SCRIPT ${LN_CUSTOM_LD_SCRIPT} CACHE INTERNAL "")

#TARGET_LINK_OPTIONS(swindle_gd_bootloader PRIVATE "-T${SCRIPT}")

#
#

message(STATUS "Configured for ${LN_MCU}")
message(STATUS "Total Flash ${LN_MCU_FLASH_SIZE}")

if(USE_GD32_CRYSTALLESS)
  set(EXTRA_NAME "_nocrystal")
endif()

set_target_properties(swindle_gd_bootloader PROPERTIES OUTPUT_NAME swindle_gd_bootloader_arm${EXTRA_NAME})

#if(USE_GD32F3)
#set_target_properties(swindle_gd_bootloader PROPERTIES OUTPUT_NAME swindle_gd_bootloader_GD32F303)
#else(USE_GD32F3)
#set_target_properties(swindle_gd_bootloader PROPERTIES OUTPUT_NAME swindle_gd_bootloader_STM32F103)
#endif(USE_GD32F3)

