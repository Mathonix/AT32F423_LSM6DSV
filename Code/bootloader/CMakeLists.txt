# 配套 Bootloader —— 纯 C 工程，链接脚本与 APP 不同（起始 0x08000000，32 KiB）。
# 由根 CMakeLists 的 add_subdirectory 引入，继承 CODE / LIB / SRC_DIR / INC_DIR / MW_DIR。
#
#   cmake --build <build-dir> --target bootloader
#
# 等价于 Makefile 的 make bootloader（每次都强制全量重建，因为 BL 只有 33 个 TU，
# 且必须确保镜像与源码一一对应）。

set(BL_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(BL_STARTUP ${LIB}/libraries/cmsis/cm4/device_support/startup/gcc/startup_at32f423.s)

# 顺序与 Code/bootloader/Makefile 的 SRCS 完全一致，理由同 APP：链接顺序决定段布局。
set(BL_SOURCES
  ${BL_DIR}/src/main.c
  ${BL_DIR}/src/bl_io.c
  ${BL_DIR}/src/bl_protocol.c
  ${BL_DIR}/src/usb_delay.c
  ${BL_DIR}/src/bl_syscalls.c
  ${LIB}/libraries/cmsis/cm4/device_support/system_at32f423.c
  ${LIB}/libraries/drivers/src/at32f423_crm.c
  ${LIB}/libraries/drivers/src/at32f423_gpio.c
  ${LIB}/libraries/drivers/src/at32f423_misc.c
  ${LIB}/libraries/drivers/src/at32f423_usart.c
  ${LIB}/libraries/drivers/src/at32f423_flash.c
  ${LIB}/libraries/drivers/src/at32f423_pwc.c
  ${LIB}/libraries/drivers/src/at32f423_acc.c
  ${LIB}/libraries/drivers/src/at32f423_usb.c
  ${MW_DIR}/usb_drivers/src/usb_core.c
  ${MW_DIR}/usb_drivers/src/usbd_core.c
  ${MW_DIR}/usb_drivers/src/usbd_int.c
  ${MW_DIR}/usb_drivers/src/usbd_sdr.c
  ${MW_DIR}/usbd_class/cdc/cdc_class.c
  ${MW_DIR}/usbd_class/cdc/cdc_desc.c
  ${BL_DIR}/src/bl_startup.c
  ${SRC_DIR}/calibration/boot_startup.c
  ${SRC_DIR}/calibration/gyro_startup_calibration.c
  ${SRC_DIR}/calibration/fusion_settings.c
  ${SRC_DIR}/calibration/acc_calibration.c
  ${SRC_DIR}/drivers/lsm6dsv.c
  ${SRC_DIR}/drivers/protocol.c
  ${SRC_DIR}/drivers/can_protocol.c
  ${SRC_DIR}/fusion/zaru_heading_hold.c
  ${LIB}/libraries/drivers/src/at32f423_spi.c
  ${LIB}/libraries/drivers/src/at32f423_scfg.c
  ${SRC_DIR}/bsp/ws2812.c
  ${BL_STARTUP}
)

add_executable(at32f423_bootloader EXCLUDE_FROM_ALL ${BL_SOURCES})

target_include_directories(at32f423_bootloader PRIVATE
  ${BL_DIR}/inc
  ${INC_DIR}/config
  ${INC_DIR}/app
  ${INC_DIR}/bsp
  ${INC_DIR}/calibration
  ${INC_DIR}/drivers
  ${INC_DIR}/fusion
  ${INC_DIR}/telemetry
  ${LIB}/libraries/cmsis/cm4/core_support
  ${LIB}/libraries/cmsis/cm4/device_support
  ${LIB}/libraries/drivers/inc
  ${MW_DIR}/usb_drivers/inc
  ${MW_DIR}/usbd_class/cdc
)

target_compile_definitions(at32f423_bootloader PRIVATE
  AT32F423KCU7_4
  USE_STDPERIPH_DRIVER
  VECT_TAB_OFFSET=0x0000
)

# 与 BL 的 Makefile 一致：不加 -fno-common / -fno-builtin。
target_compile_options(at32f423_bootloader PRIVATE
  -O1 -g -Wall
  -ffunction-sections -fdata-sections
)

# __FILE__ 的相对基准是 Code/bootloader（Makefile 从该目录编译，形如 ../src/calibration/…）。
foreach(_src IN LISTS BL_SOURCES)
  if(_src MATCHES "\\.(c|cpp)$")
    file(RELATIVE_PATH _rel "${BL_DIR}" "${_src}")
    string(REPLACE "\\" "/" _rel "${_rel}")
    set_source_files_properties("${_src}" PROPERTIES COMPILE_DEFINITIONS "__FILE__=\"${_rel}\"")
  endif()
endforeach()
target_compile_options(at32f423_bootloader PRIVATE -Wno-builtin-macro-redefined)

set(BL_OUT ${BL_DIR}/build)

set_target_properties(at32f423_bootloader PROPERTIES
  OUTPUT_NAME             at32f423_bootloader
  SUFFIX                  ".elf"
  LINKER_LANGUAGE         C
  RUNTIME_OUTPUT_DIRECTORY ${BL_OUT}
  LINK_DEPENDS            ${BL_DIR}/linker/AT32F423_boot.ld
)

target_link_options(at32f423_bootloader PRIVATE
  -T${BL_DIR}/linker/AT32F423_boot.ld
  -Wl,--gc-sections
  "-Wl,-Map=${BL_OUT}/at32f423_bootloader.map"
  --specs=nano.specs
  --specs=nosys.specs
)
target_link_libraries(at32f423_bootloader PRIVATE m)

add_custom_command(TARGET at32f423_bootloader POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:at32f423_bootloader> ${BL_OUT}/at32f423_bootloader.hex
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:at32f423_bootloader> ${BL_OUT}/at32f423_bootloader.bin
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:at32f423_bootloader>
  BYPRODUCTS ${BL_OUT}/at32f423_bootloader.hex ${BL_OUT}/at32f423_bootloader.bin
  VERBATIM
)

# 与 make bootloader 同名，方便沿用肌肉记忆。
add_custom_target(bootloader DEPENDS at32f423_bootloader)
