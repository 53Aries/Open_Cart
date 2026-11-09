# Open_Cart
idf_component_register(
    SRCS
        "main.cpp"
        "app.cpp"
        "ads1220.cpp"
        "scale.cpp"
        "settings.cpp"
        "web_server.cpp"
        "scoreboard.cpp"
        "isobus_vt.cpp"
        "status_led.cpp"
        "unload.cpp"
        "unload_log.cpp"
    INCLUDE_DIRS "."
    REQUIRES driver esp_http_server esp_wifi esp_netif esp_event esp_system nvs_flash app_update mdns led_strip
)

# Embed the ISOBUS object pool if it exists (BINARY, never TEXT: a trailing NUL makes VTs reject the pool).
set(IOP_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../isobus/scale.iop")
if(EXISTS "${IOP_FILE}")
    target_add_binary_data(${COMPONENT_LIB} "${IOP_FILE}" BINARY)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE HAVE_SCALE_IOP=1)
endif()
