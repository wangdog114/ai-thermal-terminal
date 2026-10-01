idf_component_register(
  SRCS "user_settings.cpp" "settings_store.cpp"
  INCLUDE_DIRS "include"
  REQUIRES nvs_flash
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
