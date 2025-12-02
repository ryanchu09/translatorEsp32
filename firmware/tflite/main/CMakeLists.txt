idf_component_register(
    SRCS "model_data.cc" "inference.cpp" "streamer.cpp" "main.cpp" "test_kissfft.c"
    INCLUDE_DIRS "."
    REQUIRES esp-tflite-micro driver esp_wifi esp_netif esp_http_client esp_timer nvs_flash
)
