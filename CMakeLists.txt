cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME AudioGalaxy)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# ---------------------------------------------------------------------------
# AudioGalaxy application code (App/) and code shared with BeaconGalaxy
# (common/). CubeMX owns Core/, Drivers/, Middlewares/, USB_HOST/, FATFS/.
# ---------------------------------------------------------------------------
# Shared code and tools: inside this project (stand-alone repository) or next
# to it (the combined Galaxy workspace).
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/common)
    set(COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/common)
    set(TOOLS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tools)
else()
    set(COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../common)
    set(TOOLS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../tools)
endif()

# The Discovery BSP (App/Bsp) drives the codec (I2C1 + I2S3), the microphone
# (I2S2) and the accelerometer bus (SPI1); CubeMX does not configure them, so
# their HAL modules are switched on here.
target_compile_definitions(stm32cubemx INTERFACE
    HAL_I2C_MODULE_ENABLED HAL_I2S_MODULE_ENABLED HAL_SPI_MODULE_ENABLED)
target_sources(STM32_Drivers PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_i2c.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_i2c_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_i2s.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_i2s_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_spi.c)

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/App/*.c)
set(COMMON_SOURCES
    ${COMMON_DIR}/cli/cli.c
    ${COMMON_DIR}/cli/cli_parse.c
    ${COMMON_DIR}/cli/log.c
    ${COMMON_DIR}/util/crc16.c
    ${COMMON_DIR}/util/crc32.c
    ${COMMON_DIR}/util/swd_console.c
    ${COMMON_DIR}/link/galaxy_link.c
    ${COMMON_DIR}/stm32f4/uart_io.c
    ${COMMON_DIR}/stm32f4/watchdog.c)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${APP_SOURCES} ${COMMON_SOURCES})

# Own code: extra warnings (CI fails on any warning in App/ or common/)
set(OWN_SOURCES ${APP_SOURCES} ${COMMON_SOURCES})
list(FILTER OWN_SOURCES EXCLUDE REGEX "/App/Bsp/")
set_source_files_properties(${OWN_SOURCES} PROPERTIES COMPILE_OPTIONS "-Wextra")

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Bsp/Pdm
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Main
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Console
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Link
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Player
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Bsp
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Bsp/Components
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Bsp/Pdm
    ${COMMON_DIR}/cli
    ${COMMON_DIR}/util
    ${COMMON_DIR}/link
    ${COMMON_DIR}/stm32f4
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    USE_STM32F4_DISCO
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # PDM -> PCM filter for the MEMS microphone (ST, Cortex-M4F GCC)
    PDMFilter_CM4_GCC_wc32
    m
)
