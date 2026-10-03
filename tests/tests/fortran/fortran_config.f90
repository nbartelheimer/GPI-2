program main
  use gaspi
  use, intrinsic :: iso_c_binding
  implicit none
  type(gaspi_config_t) :: config, actual
  integer(gaspi_return_t) :: ret

  ret = gaspi_config_get(config)
  if (ret /= GASPI_SUCCESS) stop 1
  if (config%network < GASPI_IB .or. config%network > GASPI_PORTALS4) stop 2
  if (config%queue_num /= 8 .or. config%queue_size_max /= 1024) stop 3
  if (config%rw_list_elem_max /= 255) stop 4
  if (c_associated(config%user_defined)) stop 5
  config%queue_num = 4
  ret = gaspi_config_set(config)
  if (ret /= GASPI_SUCCESS) stop 6
  ret = gaspi_config_get(actual)
  if (ret /= GASPI_SUCCESS) stop 7
  if (actual%queue_num /= 4 .or. actual%network /= config%network) stop 8
  ret = gaspi_proc_init(GASPI_BLOCK)
  if (ret /= GASPI_SUCCESS) stop 9
  ret = gaspi_proc_term(GASPI_BLOCK)
  if (ret /= GASPI_SUCCESS) stop 10
end program main
