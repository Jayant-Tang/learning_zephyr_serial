target_sources(app PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/app_usb.c
    ${CMAKE_CURRENT_SOURCE_DIR}/app_usb_callback.c
    )

target_include_directories(app PRIVATE .)