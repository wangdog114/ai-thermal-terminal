idf_component_register(
  SRCS "terminal_client.cpp"
  INCLUDE_DIRS "include"
  REQUIRES esp_http_client esp-tls cjson log settings tpb1 interfaces
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
