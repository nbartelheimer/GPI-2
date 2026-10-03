#include <test_utils.h>
#include <GASPI_Ext.h>

int main(int argc, char **argv)
{
  TSUITE_INIT(argc, argv);
  ASSERT(gaspi_proc_init(GASPI_BLOCK));
  gaspi_rank_t rank, nprocs;
  ASSERT(gaspi_proc_rank(&rank));
  ASSERT(gaspi_proc_num(&nprocs));
  ASSERT(gaspi_segment_create(0, 64, GASPI_GROUP_ALL, GASPI_BLOCK,
                              GASPI_MEM_INITIALIZED));
  gaspi_rank_t peer = (rank + 1) % nprocs;
  for(int pass = 0; pass < 8; ++pass)
  {
    gaspi_queue_id_t queue;
    ASSERT(gaspi_queue_create(&queue, GASPI_BLOCK));
    ASSERT(gaspi_write(0, 0, peer, 0, 8, 8, queue, GASPI_BLOCK));
    ASSERT(gaspi_wait(queue, GASPI_BLOCK));
    ASSERT(gaspi_queue_delete(queue));
  }
  ASSERT(gaspi_barrier(GASPI_GROUP_ALL, GASPI_BLOCK));
  ASSERT(gaspi_proc_term(GASPI_BLOCK));
  return EXIT_SUCCESS;
}
