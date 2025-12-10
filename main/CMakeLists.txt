idf_component_register(SRCS "app_main.c" "dht11.c" "app_support.c" 
                    INCLUDE_DIRS "."
                    REQUIRES esp_rainmaker nvs_flash esp_insights driver esp_adc esp_timer esp_https_ota app_update)
