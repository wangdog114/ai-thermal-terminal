idf_component_register(
  SRCS "app_main.cpp"
  INCLUDE_DIRS "."
  REQUIRES app_core bitmap_store settings wifi_manager terminal_client oled_display nec_input csn_a2_printer terminal_ui english_dictionary board_config console log esp_system esp_timer
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
