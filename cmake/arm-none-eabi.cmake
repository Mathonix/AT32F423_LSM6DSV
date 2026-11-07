# 交叉编译工具链：探测顺序与根 Makefile 第 15-36 行保持一致。
# 优先使用 PlatformIO 自带的 GCC 7.2.1，找不到才退回 PATH 里的 arm-none-eabi-*。
# 两套编译器产物字节不同（APP 182 172 B vs 174 320 B），换工具链等于换镜像。

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# 裸机工程：只做静态库试编译，避免 CMake 按 Windows 主机规则链接 -lkernel32 之类。
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# ARM_TOOLCHAIN 用来锁定"用哪套编译器"，因为两套产物字节不同：
#   platformio（默认）→ GCC 7.2.1，与 Makefile 的 APP 链一致，发布镜像由它产出
#   path             → PATH 里的 arm-none-eabi-gcc（本机为 15.2.1）
#                      交付包里的 bl-20261023s.bin（fa7d46a7…）是这个编的，
#                      因为 Makefile 的 BL 链从不探测 PlatformIO。
# 切换示例：cmake -DARM_TOOLCHAIN=path …
set(ARM_TOOLCHAIN "platformio" CACHE STRING "platformio | path")
set_property(CACHE ARM_TOOLCHAIN PROPERTY STRINGS "platformio" "path")

if(WIN32)
  file(TO_CMAKE_PATH "$ENV{USERPROFILE}/.platformio/packages/toolchain-gccarmnoneeabi/bin" PIO_BIN)
else()
  set(PIO_BIN "$ENV{HOME}/.platformio/packages/toolchain-gccarmnoneeabi/bin")
endif()

if(EXISTS "${PIO_BIN}/arm-none-eabi-gcc.exe")
  set(_ext ".exe")
elseif(EXISTS "${PIO_BIN}/arm-none-eabi-gcc")
  set(_ext "")
else()
  set(PIO_BIN "")
  set(_ext "")
endif()

if(ARM_TOOLCHAIN STREQUAL "path")
  set(PIO_BIN "")
endif()

if(PIO_BIN)
  set(CMAKE_C_COMPILER   "${PIO_BIN}/arm-none-eabi-gcc${_ext}")
  set(CMAKE_CXX_COMPILER "${PIO_BIN}/arm-none-eabi-g++${_ext}")
  set(CMAKE_ASM_COMPILER "${PIO_BIN}/arm-none-eabi-gcc${_ext}")
  set(CMAKE_OBJCOPY      "${PIO_BIN}/arm-none-eabi-objcopy${_ext}")
  set(CMAKE_SIZE         "${PIO_BIN}/arm-none-eabi-size${_ext}")
  set(ARM_TOOLCHAIN_SOURCE "PlatformIO (GCC 7.2.1)" CACHE INTERNAL "")
else()
  set(CMAKE_C_COMPILER   "arm-none-eabi-gcc")
  set(CMAKE_CXX_COMPILER "arm-none-eabi-g++")
  set(CMAKE_ASM_COMPILER "arm-none-eabi-gcc")
  set(CMAKE_OBJCOPY      "arm-none-eabi-objcopy")
  set(CMAKE_SIZE         "arm-none-eabi-size")
  set(ARM_TOOLCHAIN_SOURCE "PATH" CACHE INTERNAL "")
endif()

set(_CPU_FLAGS "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard")
set(CMAKE_C_FLAGS_INIT   "${_CPU_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${_CPU_FLAGS}")
set(CMAKE_ASM_FLAGS_INIT "${_CPU_FLAGS}")
