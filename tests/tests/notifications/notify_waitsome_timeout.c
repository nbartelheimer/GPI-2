#include <test_utils.h>

int
main (int argc, char *argv[])
{
  TSUITE_INIT (argc, argv);

  ASSERT (gaspi_proc_init (GASPI_BLOCK));

  gaspi_number_t notif_num;
  ASSERT (gaspi_notification_num (&notif_num));

  if (notif_num > 0)
  {
    gaspi_notification_id_t id;

    ASSERT (gaspi_segment_create (0,
                                  sizeof (char),
                                  GASPI_GROUP_ALL,
                                  GASPI_BLOCK,
                                  GASPI_MEM_UNINITIALIZED));

    EXPECT_TIMEOUT (gaspi_notify_waitsome (0, 0, 1, &id, GASPI_TEST));
    EXPECT_TIMEOUT (gaspi_notify_waitsome (0, 0, 1, &id, 10));

    ASSERT (gaspi_segment_delete (0));
  }

  ASSERT (gaspi_proc_term (GASPI_BLOCK));

  return EXIT_SUCCESS;
}