target_sources(app PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/app_uart.c
    )

target_include_directories(app PRIVATE .)