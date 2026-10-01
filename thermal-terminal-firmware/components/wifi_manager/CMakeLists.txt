idf_component_register(
  SRCS "wifi_manager.cpp"
  INCLUDE_DIRS "include"
  REQUIRES esp_wifi esp_event esp_netif freertos log settings
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
