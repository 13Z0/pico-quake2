# BootPartition.cmake -- flash map of pico-quake2.
#
# 16 MB boards:
#
#   0x10000000  pico-bootLoader (bootloader builds)          512 KB
#   0x10080000  application (bootloader builds)             2.5 MB   Q2_APP_BASE / Q2_APP_SIZE
#               (standalone builds start at 0x10000000 and may use up to 3 MB)
#   0x10300000  flash asset cache (qcommon/fcache.c)         13 MB   Q2_FCACHE_BASE / Q2_FCACHE_SIZE
#   0x11000000  end of flash (also the PSRAM XIP window on QMI CS1)
#
# The application must end below Q2_FCACHE_BASE in both variants; the linker
# script's FLASH region is sized so that it does. The cache detects entries that
# another application overwrote (per-entry CRC), so it may overlap regions other
# bootloader apps use; they only cost a partial refill.
#
# NOTE: this file does not call pico_set_linker_script. cmake/psram_linker.cmake
# owns the generated linker script (it also carries the PSRAM sections) and applies
# the FLASH region from here.

if(DEFINED _Q2_BOOTPARTITION_INCLUDED)
    return()
endif()
set(_Q2_BOOTPARTITION_INCLUDED 1)

set(Q2_XIP_BASE     "0x10000000" CACHE STRING "RP2350 XIP flash base")
set(Q2_APP_BASE     "0x10080000" CACHE STRING "Bootloader app-partition base")
set(Q2_FLASH_TOTAL  "0x1000000"  CACHE STRING "Total external flash on the board")
set(Q2_FCACHE_BASE  "0x10300000" CACHE STRING "Start of the flash asset cache (XIP address)")
math(EXPR Q2_FCACHE_SIZE "${Q2_XIP_BASE} + ${Q2_FLASH_TOTAL} - ${Q2_FCACHE_BASE}" OUTPUT_FORMAT HEXADECIMAL)
if (BUILD_FOR_BOOTLOADER)
    math(EXPR Q2_APP_SIZE "${Q2_FCACHE_BASE} - ${Q2_APP_BASE}" OUTPUT_FORMAT HEXADECIMAL)
else()
    math(EXPR Q2_APP_SIZE "${Q2_FCACHE_BASE} - ${Q2_XIP_BASE}" OUTPUT_FORMAT HEXADECIMAL)
endif()
message(STATUS "pico-quake2 flash: app ${Q2_APP_SIZE} bytes, asset cache ${Q2_FCACHE_BASE} + ${Q2_FCACHE_SIZE}")

# Target properties that go with a relinked (bootloader) image.
function(q2_bootloader_target_props TARGET)
    # The real chip capacity: hard_assert in flash_range_erase/program panics if a
    # board header understates it.
    target_compile_definitions(${TARGET} PRIVATE PICO_FLASH_SIZE_BYTES=${Q2_FLASH_TOTAL})

    # picotool >= 2.2.0 assumes RP2040 for images that do not start at 0x10000000.
    if(PICO_PLATFORM MATCHES "rp2350")
        find_program(_q2_picotool picotool)
        if(_q2_picotool)
            execute_process(COMMAND ${_q2_picotool} version --semantic
                            OUTPUT_VARIABLE _q2_pt_ver
                            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if(_q2_pt_ver VERSION_GREATER_EQUAL 2.2.0)
                set_target_properties(${TARGET} PROPERTIES
                    PICOTOOL_EXTRA_UF2_ARGS "--platform;rp2350")
            endif()
        endif()
    endif()
endfunction()
