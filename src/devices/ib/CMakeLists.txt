cmake_minimum_required(VERSION 3.5)

find_package(IBVerbs REQUIRED)

add_library(
  ib-device STATIC "GPI2_IB.c" "GPI2_IB_ATOMIC.c" "GPI2_IB_GRP.c"
  "GPI2_IB_IO.c" "GPI2_IB_PASSIVE.c" "GPI2_IB_SEG.c"
)

target_include_directories(
  ib-device PUBLIC "../include" "${CMAKE_SOURCE_DIR}/src"
                   "${CMAKE_SOURCE_DIR}/src/include"
)
target_link_libraries(ib-device PUBLIC "IBVerbs::IBVerbs")
target_compile_features(ib-device PUBLIC c_std_11)
target_compile_options(ib-device PUBLIC "-fPIC")
target_compile_definitions(
  ib-device PUBLIC "GPI2_DEVICE_IB" "$<$<CONFIG:DEBUG>:DEBUG>"
)
include(GNUInstallDirs)
install(TARGETS ib-device DESTINATION ${CMAKE_INSTALL_LIBDIR})
