cmake_minimum_required(VERSION 3.5)

set(GPI2_LIB "${CMAKE_BINARY_DIR}/src/libGPI-2.so")
set(APPS
    "max_mem"
    "seg_alloc_all"
    "seg_alloc_all_usermsegs"
    "seg_alloc_diff"
    "seg_avail_locally"
    "seg_create_all"
    "seg_create_all_usermsegs"
    "seg_create_same"
    "seg_id_list"
    "seg_reuse"
    "seg_reuse_buffer"
    "seg_subsegments"
    "seg_use_buffer"
)

foreach(APP IN LISTS APPS)
  if(APP STREQUAL "")
    continue()
  else()
    if(APP STREQUAL "seg_shmem")
      add_executable(${APP}.bin "${APP}.cpp")
    else()
      add_executable(${APP}.bin "${APP}.c")
    endif()
    target_link_libraries(${APP}.bin PUBLIC "${GPI2_LIB}" "test_utils")
    target_include_directories(${APP}.bin PUBLIC "../")
  endif()
endforeach()
