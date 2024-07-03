cmake_minimum_required(VERSION 3.5)
project(GPI-2)

option(WITH_IB "Build IB device" OFF)
option(WITH_PORTALS "Build PORTALS device" OFF)
option(WITH_ETHERNET "Build Ethernet device" OFF)
option(WITH_MPI "Enable MPI Interoperability" OFF)
option(WITH_SLURM "Slurm support" ON)

option(BUILD_TESTS "Build tests" OFF)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake;${CMAKE_MODULE_PATH}")

add_subdirectory(src)

if(WITH_PORTALS)
  add_subdirectory(tools)
endif()

if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

include(GNUInstallDirs)

if(WITH_SLURM)
  install(FILES "bin/gaspi_run.slurm" PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ DESTINATION ${CMAKE_INSTALL_BINDIR} RENAME "gaspi_run")
  install(FILES "bin/slurm.env" "bin/gaspi_cleanup" PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

