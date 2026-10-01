idf_component_register(
  SRCS "english_dictionary.cpp"
  INCLUDE_DIRS "include"
  REQUIRES spiffs
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
