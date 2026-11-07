# AT32F423KCU7-4 + LSM6DSV/IST8310 —— CMake + Ninja 构建
#
# 与根 Makefile 等价的一份实现，两者产物逐字节相同：
#   cmake --preset 9axis && cmake --build --preset 9axis
# 等价于
#   mingw32-make -j4
#
# 产物仍落在 build/<TARGET>/<mode>-<axes>/，PC 侧脚本（烧录/调参/长采样）不用改路径。

cmake_minimum_required(VERSION 3.20)

project(at32f423_lsm6dsv LANGUAGES C CXX ASM)

message(STATUS "ARM toolchain : ${ARM_TOOLCHAIN_SOURCE}, compiler ${CMAKE_C_COMPILER_VERSION}")

set(CODE     ${CMAKE_CURRENT_SOURCE_DIR}/Code)
set(LIB      ${CODE}/AT32F423_Firmware_Library)
set(SRC_DIR  ${CODE}/src)
set(INC_DIR  ${CODE}/inc)
set(MW_DIR   ${CODE}/middleware)
set(LDSCRIPT ${CODE}/linker/AT32F423_app.ld)
set(STARTUP  ${LIB}/libraries/cmsis/cm4/device_support/startup/gcc/startup_at32f423.s)

# ---------------------------------------------------------------- 选项
set(TARGET_NAME "lsm6dsv_spi_test" CACHE STRING "输出文件名")
set(APP_MAIN    "app/main.c"       CACHE STRING "入口源文件，可换成 diagnostics 下的独立 main")
option(SIX_AXIS           "六轴构建：关磁融合，目录变 release-6axis"      OFF)
option(DEBUG_BUILD        "调试构建：开陀螺快启，目录变 debug-9axis"      OFF)
option(USER_BL_UPDATER    "维护固件：内嵌 32 KiB Bootloader"             OFF)
option(RELATIVE_FILE_MACRO "把 __FILE__ 固定为仓库相对路径（保证字节可复现）" ON)
set(EXTRA_CFLAGS "" CACHE STRING "追加编译选项，空格或分号分隔，例如 -DAPP_FUSION_HZ=1000")

# 不让 CMake 按构建类型注入 -O3 / -DNDEBUG：优化等级完全由下面逐条给出。
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Firmware CACHE STRING "不做任何预设，保持与 Makefile 一致" FORCE)
endif()
foreach(_cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL FIRMWARE)
  set(CMAKE_C_FLAGS_${_cfg}   "" CACHE STRING "" FORCE)
  set(CMAKE_CXX_FLAGS_${_cfg} "" CACHE STRING "" FORCE)
  set(CMAKE_ASM_FLAGS_${_cfg} "" CACHE STRING "" FORCE)
endforeach()

# ---------------------------------------------------------------- 源文件
# 顺序与 Makefile 的 SRCS / CPPSRCS 完全一致：链接时目标文件顺序会影响
# 段布局，进而决定 .bin 的字节，不能随意重排。
set(APP_SOURCES
  ${SRC_DIR}/${APP_MAIN}
  ${SRC_DIR}/bsp/bsp.c
  ${SRC_DIR}/drivers/lsm6dsv.c
  ${SRC_DIR}/drivers/lsm6dsv_8k.c
  ${SRC_DIR}/drivers/ist8310.c
  ${SRC_DIR}/calibration/gyro_bias_history.c
  ${SRC_DIR}/calibration/gyro_temp_comp.c
  ${SRC_DIR}/calibration/gyro_temp_learning.c
  ${SRC_DIR}/calibration/gyro_startup_calibration.c
  ${SRC_DIR}/calibration/boot_startup.c
  ${SRC_DIR}/calibration/acc_calibration.c
  ${SRC_DIR}/calibration/acc_six_face.c
  ${SRC_DIR}/calibration/fusion_settings.c
  ${SRC_DIR}/calibration/vqf_static_cal.c
  ${SRC_DIR}/fusion/attitude_output.c
  ${SRC_DIR}/fusion/gyro_prefilter.c
  ${SRC_DIR}/bsp/static6_diag.c
  ${SRC_DIR}/bsp/perf_meter.c
  ${SRC_DIR}/fusion/zaru_heading_hold.c
  ${SRC_DIR}/bsp/ws2812.c
  ${SRC_DIR}/drivers/can_test.c
  ${SRC_DIR}/drivers/can_protocol.c
  ${SRC_DIR}/drivers/usb_cdc.c
  ${MW_DIR}/usb_drivers/src/usb_core.c
  ${MW_DIR}/usb_drivers/src/usbd_core.c
  ${MW_DIR}/usb_drivers/src/usbd_int.c
  ${MW_DIR}/usb_drivers/src/usbd_sdr.c
  ${MW_DIR}/usbd_class/cdc/cdc_class.c
  ${MW_DIR}/usbd_class/cdc/cdc_desc.c
  ${SRC_DIR}/drivers/protocol.c
  ${SRC_DIR}/drivers/inertial_stream.c
  ${SRC_DIR}/drivers/data_capture.c
  ${SRC_DIR}/drivers/user_bl_update.c
  ${SRC_DIR}/bsp/at32f423_clock.c
  ${SRC_DIR}/bsp/at32f423_int.c
  ${LIB}/libraries/cmsis/cm4/device_support/system_at32f423.c
  ${LIB}/libraries/drivers/src/at32f423_crm.c
  ${LIB}/libraries/drivers/src/at32f423_gpio.c
  ${LIB}/libraries/drivers/src/at32f423_misc.c
  ${LIB}/libraries/drivers/src/at32f423_spi.c
  ${LIB}/libraries/drivers/src/at32f423_scfg.c
  ${LIB}/libraries/drivers/src/at32f423_usart.c
  ${LIB}/libraries/drivers/src/at32f423_flash.c
  ${LIB}/libraries/drivers/src/at32f423_pwc.c
  ${LIB}/libraries/drivers/src/at32f423_dma.c
  ${LIB}/libraries/drivers/src/at32f423_can.c
  ${LIB}/libraries/drivers/src/at32f423_usb.c
  ${LIB}/libraries/drivers/src/at32f423_acc.c
  ${SRC_DIR}/fusion/ekf.cpp
  ${SRC_DIR}/fusion/vqf_wrapper.cpp
  ${SRC_DIR}/fusion/eskf8k.cpp
  ${SRC_DIR}/fusion/vqf_full.cpp
  ${STARTUP}
)

add_executable(${TARGET_NAME} ${APP_SOURCES})

# ---------------------------------------------------------------- 头文件
target_include_directories(${TARGET_NAME} PRIVATE
  ${INC_DIR}
  ${INC_DIR}/app
  ${INC_DIR}/bsp
  ${INC_DIR}/calibration
  ${INC_DIR}/config
  ${INC_DIR}/drivers
  ${INC_DIR}/fusion
  ${INC_DIR}/telemetry
  ${LIB}/libraries/cmsis/cm4/core_support
  ${LIB}/libraries/cmsis/cm4/device_support
  ${LIB}/libraries/drivers/inc
  ${MW_DIR}/usb_drivers/inc
  ${MW_DIR}/usbd_class/cdc
)

if(USER_BL_UPDATER)
  target_compile_definitions(${TARGET_NAME} PRIVATE APP_USER_BL_UPDATE)
  target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/build/user-bl-updater)
endif()

# ---------------------------------------------------------------- 宏
target_compile_definitions(${TARGET_NAME} PRIVATE
  AT32F423KCU7_4
  USE_STDPERIPH_DRIVER
  VECT_TAB_OFFSET=0x8000
  APP_GYR_FAST_START_ENABLE=$<BOOL:${DEBUG_BUILD}>
  APP_MAG_FUSION_ENABLE=$<IF:$<BOOL:${SIX_AXIS}>,0,1>
)

# ---------------------------------------------------------------- 编译选项
target_compile_options(${TARGET_NAME} PRIVATE
  -O1 -g -Wall
  -ffunction-sections -fdata-sections
  -fno-common -fno-builtin
)

# C++ 追加：写在 -O1 之后，以最后一个 -O 为准。
foreach(_flag -O3 -DVQF_SINGLE_PRECISION -std=gnu++14 -fno-exceptions -fno-rtti)
  target_compile_options(${TARGET_NAME} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${_flag}>)
endforeach()

if(EXTRA_CFLAGS)
  separate_arguments(_extra WINDOWS_COMMAND "${EXTRA_CFLAGS}")
  target_compile_options(${TARGET_NAME} PRIVATE ${_extra})
endif()

# 少数热点 TU 单独提到 -O3，并保留 .su 栈用量证据。
set(_HOT_TUS main.c perf_meter.c bsp.c protocol.c lsm6dsv.c)
foreach(_src IN LISTS APP_SOURCES)
  get_filename_component(_name "${_src}" NAME)
  if(_name IN_LIST _HOT_TUS)
    set_source_files_properties("${_src}" PROPERTIES COMPILE_OPTIONS "-O3;-fstack-usage")
  endif()
endforeach()

# CMake 传给编译器的源路径是绝对的，而 assert() 会把 __FILE__ 写进 .rodata。
# 这里按仓库相对路径把 __FILE__ 钉住，产出的字节才与 Makefile 版一致，
# 也避免把本机目录泄漏进固件。GCC 7.2.1 没有 -ffile-prefix-map，只能用这招。
if(RELATIVE_FILE_MACRO)
  foreach(_src IN LISTS APP_SOURCES)
    if(_src MATCHES "\\.(c|cpp)$")
      file(RELATIVE_PATH _rel "${CMAKE_CURRENT_SOURCE_DIR}" "${_src}")
      string(REPLACE "\\" "/" _rel "${_rel}")
      set_source_files_properties("${_src}" PROPERTIES COMPILE_DEFINITIONS "__FILE__=\"${_rel}\"")
    endif()
  endforeach()
  target_compile_options(${TARGET_NAME} PRIVATE -Wno-builtin-macro-redefined)
endif()

# ---------------------------------------------------------------- 输出目录
if(DEBUG_BUILD)
  set(_mode debug)
else()
  set(_mode release)
endif()
if(SIX_AXIS)
  set(_axes 6axis)
else()
  set(_axes 9axis)
endif()
set(OUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/build/${TARGET_NAME}/${_mode}-${_axes})

set_target_properties(${TARGET_NAME} PROPERTIES
  OUTPUT_NAME             ${TARGET_NAME}
  SUFFIX                  ".elf"
  LINKER_LANGUAGE         CXX
  RUNTIME_OUTPUT_DIRECTORY ${OUT_DIR}
  LINK_DEPENDS            ${LDSCRIPT}
)

# ---------------------------------------------------------------- 链接
target_link_options(${TARGET_NAME} PRIVATE
  -T${LDSCRIPT}
  -Wl,--gc-sections
  "-Wl,-Map=${OUT_DIR}/${TARGET_NAME}.map"
  --specs=nano.specs
  --specs=nosys.specs
)
target_link_libraries(${TARGET_NAME} PRIVATE m)

add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:${TARGET_NAME}> ${OUT_DIR}/${TARGET_NAME}.hex
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${TARGET_NAME}> ${OUT_DIR}/${TARGET_NAME}.bin
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${TARGET_NAME}>
  BYPRODUCTS ${OUT_DIR}/${TARGET_NAME}.hex ${OUT_DIR}/${TARGET_NAME}.bin
  VERBATIM
)

# ---------------------------------------------------------------- Bootloader
# 默认不参与 all，需显式构建：cmake --build --preset bootloader
add_subdirectory(Code/bootloader)
