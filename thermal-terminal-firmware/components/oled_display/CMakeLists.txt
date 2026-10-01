idf_component_register(
  SRCS "bitmap_window.cpp" "ssd1306_display.cpp"
  INCLUDE_DIRS "include"
  REQUIRES esp_driver_i2c interfaces log
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
