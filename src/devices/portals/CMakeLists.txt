cmake_minimum_required(VERSION 3.5)

find_package(Portals REQUIRED)

add_library(
	portals-device STATIC "GPI2_PORTALS.c" "GPI2_PORTALS_ATOMIC.c" "GPI2_PORTALS_SEG.c" "GPI2_PORTALS_IO.c"
	"GPI2_PORTALS_PASSIVE.c" "GPI2_PORTALS_GRP.c" "GPI2_PORTALS_VECTOR_ACCESS.c")

target_include_directories(portals-device PUBLIC "../include" "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/src/include")
target_link_libraries(portals-device PUBLIC "Portals::Portals")
target_compile_features(portals-device PUBLIC c_std_11)
target_compile_definitions(portals-device PUBLIC "GPI2_DEVICE_PORTALS" "$<$<CONFIG:DEBUG>:DEBUG>")

include(GNUInstallDirs)
install(TARGETS portals-device DESTINATION ${CMAKE_INSTALL_LIBDIR})
