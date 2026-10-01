idf_component_register(
  SRCS "terminal_ui.cpp"
  INCLUDE_DIRS "include"
  REQUIRES oled_display interfaces settings board_config english_dictionary
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
