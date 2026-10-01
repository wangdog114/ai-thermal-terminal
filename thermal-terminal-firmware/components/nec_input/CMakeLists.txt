idf_component_register(
  SRCS "nec_decoder.cpp" "nec_receiver.cpp" "remote_keymap.cpp"
  INCLUDE_DIRS "include"
  REQUIRES esp_driver_rmt esp_timer freertos log interfaces board_config
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
