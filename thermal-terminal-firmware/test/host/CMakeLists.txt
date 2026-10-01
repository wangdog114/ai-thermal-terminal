cmake_minimum_required(VERSION 3.16)
project(thermal_terminal_host_tests LANGUAGES CXX)

enable_testing()

add_library(tpb1_core
  ../../components/tpb1/crc32.cpp
  ../../components/tpb1/packbits.cpp
  ../../components/tpb1/parser.cpp
  ../../components/settings/user_settings.cpp
  ../../components/bitmap_store/bitmap_store.cpp
  ../../components/oled_display/bitmap_window.cpp
  ../../components/nec_input/nec_decoder.cpp
  ../../components/nec_input/remote_keymap.cpp
  ../../components/terminal_ui/terminal_ui.cpp
  ../../components/english_dictionary/english_dictionary.cpp
)

target_include_directories(tpb1_core PUBLIC
  ../../components/tpb1/include
  ../../components/settings/include
  ../../components/app_core/include
  ../../components/interfaces/include
  ../../components/bitmap_store/include
  ../../components/oled_display/include
  ../../components/nec_input/include
  ../../components/board_config/include
  ../../components/terminal_ui/include
  ../../components/english_dictionary/include
)
target_compile_features(tpb1_core PUBLIC cxx_std_17)
target_compile_options(tpb1_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(tpb1_tests tpb1_test.cpp)
target_link_libraries(tpb1_tests PRIVATE tpb1_core)
target_compile_features(tpb1_tests PRIVATE cxx_std_17)
target_compile_options(tpb1_tests PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME tpb1_tests COMMAND tpb1_tests)

add_executable(input_display_tests input_display_test.cpp)
target_link_libraries(input_display_tests PRIVATE tpb1_core)
target_compile_features(input_display_tests PRIVATE cxx_std_17)
target_compile_options(input_display_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME input_display_tests COMMAND input_display_tests)
