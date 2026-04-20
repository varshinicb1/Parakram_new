cmake_minimum_required(VERSION 3.13)

# Parakram Firmware — RP2040 port
# Build: cmake -B build -DPICO_SDK_PATH=/path/to/pico-sdk && cmake --build build

set(PARAKRAM_TARGET_MCU "rp2040" CACHE STRING "Target MCU")

# Pull in Pico SDK (must be set before project())
include($ENV{PICO_SDK_PATH}/pico_sdk_init.cmake)

project(parakram_rp2040 C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# Collect all driver sources
file(GLOB DRIVER_SRCS
    "${CMAKE_SOURCE_DIR}/../../main/drivers/drv_*.c"
)

add_executable(parakram_rp2040
    main_rp2040.c
    ${DRIVER_SRCS}

    # HAL
    ../../hal/rp2040/pal_impl.c
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

target_compile_definitions(parakram_rp2040 PRIVATE
    PARAKRAM_TARGET_MCU_RP2040=1
    PARAKRAM_VERSION_MAJOR=1
    PARAKRAM_VERSION_MINOR=0
)

target_include_directories(parakram_rp2040 PRIVATE
    ../../hal/include
    ../../main/include
    ../../main/drivers
    .
)

target_link_libraries(parakram_rp2040
    pico_stdlib
    hardware_i2c
    hardware_spi
    hardware_pwm
    hardware_adc
    hardware_uart
    hardware_gpio
    hardware_flash
    hardware_sync
    hardware_irq
    pico_multicore
    pico_flash
    m
)

# USB serial stdout
pico_enable_stdio_usb(parakram_rp2040 1)
pico_enable_stdio_uart(parakram_rp2040 0)

# Generate .uf2 for drag-and-drop flashing
pico_add_extra_outputs(parakram_rp2040)
