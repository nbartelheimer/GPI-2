#include <pthread.h>
#include <test_utils.h>

/* Test that a second concurrent gaspi_proc_init correctly returns
 * GASPI_ERR_INITED after the first init has completed. */

static void *
init_thread (void *arg)
{
  gaspi_return_t *result = (gaspi_return_t *) arg;

  *result = gaspi_proc_init (GASPI_BLOCK);

  return NULL;
}

int
main (int argc, char *argv[])
{
  TSUITE_INIT (argc, argv);

  ASSERT (gaspi_proc_init (GASPI_BLOCK));

  /* Try to init again from a second thread */
  gaspi_return_t second_result = GASPI_SUCCESS;
  pthread_t t;

  pthread_create (&t, NULL, init_thread, &second_result);
  pthread_join (t, NULL);

  assert (second_result == GASPI_ERR_INITED);

  ASSERT (gaspi_proc_term (GASPI_BLOCK));

  return EXIT_SUCCESS;
}
