cmake_minimum_required(VERSION 3.22)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

project(stm32f769i_disco_psk_modem C ASM)

file(GLOB_RECURSE LVGL_SRC CONFIGURE_DEPENDS
    third_party/lvgl/src/*.c
)

add_library(lvgl STATIC ${LVGL_SRC})
target_compile_definitions(lvgl PUBLIC
    LV_CONF_INCLUDE_SIMPLE
    USE_HAL_DRIVER
    STM32F769xx
)
target_include_directories(lvgl PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_party/lvgl
    ${CMAKE_SOURCE_DIR}/Core/Inc
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F7xx_HAL_Driver/Inc
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F7xx_HAL_Driver/Inc/Legacy
    ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Device/ST/STM32F7xx/Include
    ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Include
)

add_executable(${PROJECT_NAME})

target_compile_definitions(${PROJECT_NAME} PRIVATE
    USE_HAL_DRIVER
    STM32F769xx
    $<$<CONFIG:Debug>:DEBUG>
)

target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
    -ffunction-sections
    -fdata-sections
    -fno-common
)

target_include_directories(${PROJECT_NAME} PRIVATE
    Core/Inc
    app
    app/dsp
    app/modem
    app/platform
    app/ui
    third_party/psk_rtty/include
    third_party/otm8009a
    Drivers/STM32F7xx_HAL_Driver/Inc
    Drivers/STM32F7xx_HAL_Driver/Inc/Legacy
    Drivers/CMSIS/Device/ST/STM32F7xx/Include
    Drivers/CMSIS/Include
)

set(HAL_SRC
    Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal.c
    Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_cortex.c
    Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_dma.c
    Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_dma2d.c
    Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_dsi.c
    Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_flash.c
    Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_flash_ex.c
    Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_gpio.c
    Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_i2c.c
    Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_i2c_ex.c
    Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_ltdc.c
    Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_pwr.c
    Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_pwr_ex.c
    Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_rcc.c
    Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_rcc_ex.c
    Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_sai.c
    Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_sai_ex.c
    Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_sdram.c
    Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_tim.c
    Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_tim_ex.c
    Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_uart.c
    Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_uart_ex.c
    Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_ll_fmc.c
)

target_sources(${PROJECT_NAME} PRIVATE
    startup_stm32f769xx.s
    Core/Src/main.c
    Core/Src/stm32f7xx_hal_msp.c
    Core/Src/stm32f7xx_it.c
    Core/Src/stm32f7xx_hal_timebase_tim.c
    Core/Src/system_stm32f7xx.c
    Core/Src/syscalls.c
    Core/Src/sysmem.c
    app/dsp/spectrum.c
    app/modem/psk_modem.c
    app/platform/audio_input.c
    app/platform/codec_wm8994.c
    app/ui/display.c
    third_party/psk_rtty/psk_rtty.c
    third_party/otm8009a/otm8009a.c
    ${HAL_SRC}
)

set_source_files_properties(
    app/dsp/spectrum.c
    app/platform/audio_input.c
    third_party/psk_rtty/psk_rtty.c
    PROPERTIES COMPILE_OPTIONS "$<$<CONFIG:Debug>:-O2>"
)

target_link_options(${PROJECT_NAME} PRIVATE
    -T${CMAKE_SOURCE_DIR}/STM32F769xx_FLASH.ld
    -Wl,-Map=${PROJECT_NAME}.map
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    --specs=nano.specs
)

target_link_libraries(${PROJECT_NAME} PRIVATE lvgl m)

get_filename_component(TOOLCHAIN_BIN_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
find_program(ARM_SIZE_TOOL
    NAMES ${CMAKE_SIZE} arm-none-eabi-size starm-size
    HINTS "${TOOLCHAIN_BIN_DIR}"
)

if(ARM_SIZE_TOOL)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${ARM_SIZE_TOOL} $<TARGET_FILE:${PROJECT_NAME}>
    )
endif()
