cmake_minimum_required(VERSION 3.5)

find_package(Portals REQUIRED)

add_library(
	portals-device STATIC "GPI2_PORTALS4.c" "GPI2_PORTALS4_ATOMIC.c" "GPI2_PORTALS4_SEG.c" "GPI2_PORTALS4_IO.c"
	"GPI2_PORTALS4_PASSIVE.c" "GPI2_PORTALS4_GRP.c")

target_include_directories(portals-device PUBLIC "../include" "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/src/include")
target_link_libraries(portals-device PUBLIC "Portals::Portals")
target_compile_features(portals-device PUBLIC c_std_11)
target_compile_definitions(portals-device PUBLIC "GPI2_DEVICE_PORTALS4" "$<$<CONFIG:DEBUG>:DEBUG>")
target_compile_options(portals-device PUBLIC "-fPIC")

include(GNUInstallDirs)
install(TARGETS portals-device DESTINATION ${CMAKE_INSTALL_LIBDIR})
