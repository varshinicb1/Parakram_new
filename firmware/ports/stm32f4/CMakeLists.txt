cmake_minimum_required(VERSION 3.16)

# Parakram Firmware — STM32F4 port
# Toolchain: arm-none-eabi-gcc
# Build:
#   cmake -B build \
#         -DCMAKE_TOOLCHAIN_FILE=../../cmake/arm-none-eabi.cmake \
#         -DSTM32CUBE_PATH=/path/to/STM32CubeF4 \
#   && cmake --build build

set(PARAKRAM_TARGET_MCU "stm32f4" CACHE STRING "Target MCU")

project(parakram_stm32f4 C ASM)
set(CMAKE_C_STANDARD 11)

set(STM32CUBE_PATH "" CACHE PATH "Path to STM32CubeF4 repository")
if(NOT STM32CUBE_PATH)
    message(FATAL_ERROR "Set -DSTM32CUBE_PATH=/path/to/STM32CubeF4")
endif()

# ── HAL sources from STM32Cube ─────────────────────────────────────────────
set(CUBE_DRIVERS ${STM32CUBE_PATH}/Drivers)
file(GLOB HAL_SRCS
    "${CUBE_DRIVERS}/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal*.c"
)
list(FILTER HAL_SRCS EXCLUDE REGEX ".*template.*")

# ── Collect all driver sources ─────────────────────────────────────────────
file(GLOB DRIVER_SRCS
    "${CMAKE_SOURCE_DIR}/../../main/drivers/drv_*.c"
)

add_executable(parakram_stm32f4.elf
    main_stm32f4.c
    startup_stm32f407xx.s
    ${DRIVER_SRCS}
    ${HAL_SRCS}

    # Parakram PAL
    ../../hal/stm32/pal_impl.c
    ../../hal/common/pal_onewire.c

    # Firmware runtime
    ../../main/runtime/vm.c
    ../../main/runtime/scheduler.c
    ../../main/runtime/event_bus.c
    ../../main/runtime/state_store.c
    ../../main/drivers/driver_registry.c
    ../../main/safety/watchdog.c
    ../../main/safety/rate_limiter.c
    ../../main/safety/fault_handler.c
)

target_compile_definitions(parakram_stm32f4.elf PRIVATE
    STM32F407xx
    USE_HAL_DRIVER
    HSE_VALUE=8000000U
    PARAKRAM_TARGET_MCU_STM32F4=1
    PARAKRAM_VERSION_MAJOR=1
    PARAKRAM_VERSION_MINOR=0
)

target_include_directories(parakram_stm32f4.elf PRIVATE
    ../../hal/include
    ../../hal/stm32
    ../../main/include
    ../../main/drivers
    ${CUBE_DRIVERS}/STM32F4xx_HAL_Driver/Inc
    ${CUBE_DRIVERS}/STM32F4xx_HAL_Driver/Inc/Legacy
    ${CUBE_DRIVERS}/CMSIS/Device/ST/STM32F4xx/Include
    ${CUBE_DRIVERS}/CMSIS/Include
    .
)

target_compile_options(parakram_stm32f4.elf PRIVATE
    -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
    -ffunction-sections -fdata-sections -Wall -Wno-unused-parameter
    -Os
)

target_link_options(parakram_stm32f4.elf PRIVATE
    -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
    -specs=nano.specs -specs=nosys.specs
    -T${CMAKE_SOURCE_DIR}/STM32F407ZGTx_FLASH.ld
    -Wl,--gc-sections
    -Wl,-Map=${PROJECT_NAME}.map
)

target_link_libraries(parakram_stm32f4.elf m)

# ── Post-build: .hex and .bin ──────────────────────────────────────────────
add_custom_command(TARGET parakram_stm32f4.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:parakram_stm32f4.elf> parakram_stm32f4.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:parakram_stm32f4.elf> parakram_stm32f4.bin
    COMMENT "Generating .hex and .bin"
)
