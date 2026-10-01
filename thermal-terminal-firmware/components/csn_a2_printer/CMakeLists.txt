idf_component_register(
  SRCS "csn_a2_printer.cpp"
  INCLUDE_DIRS "include"
  REQUIRES esp_driver_uart interfaces log
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
