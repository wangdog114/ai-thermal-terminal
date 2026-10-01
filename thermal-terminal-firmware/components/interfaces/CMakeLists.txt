idf_component_register(INCLUDE_DIRS "include" REQUIRES settings tpb1)

target_compile_features(${COMPONENT_LIB} INTERFACE cxx_std_17)
