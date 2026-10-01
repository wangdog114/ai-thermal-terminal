idf_component_register(
  SRCS "bitmap_store.cpp"
  INCLUDE_DIRS "include"
  REQUIRES tpb1 heap
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
