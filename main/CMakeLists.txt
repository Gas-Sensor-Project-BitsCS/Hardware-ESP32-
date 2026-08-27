idf_component_register(SRCS 
                        "mining-project.c"
                        "wifi_connect/wifi_connect.c"
                        "http_client/http_client.c"
                    INCLUDE_DIRS "."
                                 "wifi_connect"
                                 "http_client"
                    REQUIRES
                        esp_adc
                        esp_wifi
                        esp_netif
                        esp_event
                        nvs_flash
                        esp_http_client
                        json
                        driver
                    )
