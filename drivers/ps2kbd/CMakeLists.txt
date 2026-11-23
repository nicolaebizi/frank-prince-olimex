add_library(ps2kbd STATIC)

if(BOARD_VARIANT STREQUAL "OLIMEX")
    # Olimex RP2350PC: no PS/2 connector (GPIO0/1 = UEXT debug UART, must not
    # be claimed by the PS/2 PIO driver). Build ONLY the wrapper, with no-op
    # stubs; ps2kbd_mrmltr.cpp and the PIO programs are not compiled, so
    # nothing touches pio0/GPIO0/GPIO1.
    target_sources(ps2kbd PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/ps2kbd_wrapper.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ps2kbd_wrapper.h
    )
    target_compile_definitions(ps2kbd PRIVATE BOARD_OLIMEX)
    # Keep the same link dependencies as the M1/M2 builds: board_config.h
    # (included by the wrapper) needs the hardware/ include paths.
    target_link_libraries(ps2kbd PRIVATE hardware_pio hardware_clocks)
else()
    target_sources(ps2kbd PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/ps2kbd_mrmltr.cpp
    ${CMAKE_CURRENT_LIST_DIR}/ps2kbd_mrmltr.h
        ${CMAKE_CURRENT_LIST_DIR}/ps2kbd_wrapper.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ps2kbd_wrapper.h
    )

    target_link_libraries(ps2kbd PRIVATE hardware_pio hardware_clocks)

    # Add board variant define and KBD_CLOCK_PIN for PIO program selection
    if(BOARD_VARIANT STREQUAL "M2")
        target_compile_definitions(ps2kbd PRIVATE BOARD_M2 KBD_CLOCK_PIN=2)
    else()
        target_compile_definitions(ps2kbd PRIVATE BOARD_M1 KBD_CLOCK_PIN=0)
    endif()

    pico_generate_pio_header(ps2kbd
    ${CMAKE_CURRENT_LIST_DIR}/ps2kbd_mrmltr.pio
    )

    pico_generate_pio_header(ps2kbd
    ${CMAKE_CURRENT_LIST_DIR}/ps2kbd_mrmltr2.pio
    )
endif()

target_include_directories(ps2kbd PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}
)

target_include_directories(ps2kbd PRIVATE
    ../../src
)
