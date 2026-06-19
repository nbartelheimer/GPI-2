#include <pthread.h>

#include <test_utils.h>

typedef struct
{
  gaspi_segment_id_t seg_id;
  gaspi_notification_t value;
  gaspi_notification_t expected;
} wait_ctx_t;

static void *
wait_for_notification (void *arg)
{
  wait_ctx_t *const ctx = (wait_ctx_t *) arg;
  gaspi_notification_id_t id;

  ASSERT (gaspi_notify_waitsome (ctx->seg_id, 0, 1, &id, GASPI_BLOCK));
  ASSERT (gaspi_notify_reset (ctx->seg_id, id, &ctx->value));

  return NULL;
}

int
main (int argc, char *argv[])
{
  TSUITE_INIT (argc, argv);

  ASSERT (gaspi_proc_init (GASPI_BLOCK));

  gaspi_rank_t rank, nranks;
  ASSERT (gaspi_proc_num (&nranks));
  ASSERT (gaspi_proc_rank (&rank));

  ASSERT (gaspi_segment_create (0,
                                sizeof (int),
                                GASPI_GROUP_ALL,
                                GASPI_BLOCK,
                                GASPI_MEM_UNINITIALIZED));
  ASSERT (gaspi_segment_create (1,
                                sizeof (int),
                                GASPI_GROUP_ALL,
                                GASPI_BLOCK,
                                GASPI_MEM_UNINITIALIZED));

  pthread_t threads[2];
  wait_ctx_t wait_ctxs[2] = {
    {.seg_id = 0, .value = 0, .expected = 1},
    {.seg_id = 1, .value = 0, .expected = 2}
  };

  if (nranks > 1 && rank == 0)
  {
    for (int i = 0; i < 2; i++)
    {
      assert (pthread_create (&threads[i], NULL,
                              wait_for_notification, &wait_ctxs[i]) == 0);
    }
  }

  ASSERT (gaspi_barrier (GASPI_GROUP_ALL, GASPI_BLOCK));

  if (nranks > 1 && rank == 1)
  {
    ASSERT (gaspi_notify (0, 0, 0, wait_ctxs[0].expected, 0, GASPI_BLOCK));
    ASSERT (gaspi_notify (1, 0, 0, wait_ctxs[1].expected, 0, GASPI_BLOCK));
    ASSERT (gaspi_wait (0, GASPI_BLOCK));
  }

  if (nranks > 1 && rank == 0)
  {
    for (int i = 0; i < 2; i++)
    {
      assert (pthread_join (threads[i], NULL) == 0);
      assert (wait_ctxs[i].value == wait_ctxs[i].expected);
    }
  }

  ASSERT (gaspi_barrier (GASPI_GROUP_ALL, GASPI_BLOCK));
  ASSERT (gaspi_proc_term (GASPI_BLOCK));

  return EXIT_SUCCESS;
}