#include <test_utils.h>

int main(int argc, char **argv)
{
  TSUITE_INIT(argc, argv);
  ASSERT(gaspi_proc_init(GASPI_BLOCK));
  gaspi_rank_t rank, nprocs;
  ASSERT(gaspi_proc_rank(&rank));
  ASSERT(gaspi_proc_num(&nprocs));
  ASSERT(gaspi_segment_create(0, 4096, GASPI_GROUP_ALL, GASPI_BLOCK,
                              GASPI_MEM_INITIALIZED));
  gaspi_pointer_t ptr;
  ASSERT(gaspi_segment_ptr(0, &ptr));
  unsigned int *data = ptr;
  gaspi_rank_t peer = (rank + 1) % nprocs;
  for(unsigned int round = 0; round < 40; ++round)
  {
    for(unsigned int msg = 0; msg < 32; ++msg)
    {
      data[msg] = round * 32 + msg + 1;
      ASSERT(gaspi_passive_send(0, msg * sizeof(*data), peer,
                                 sizeof(*data), GASPI_BLOCK));
    }
    ASSERT(gaspi_barrier(GASPI_GROUP_ALL, GASPI_BLOCK));
    for(unsigned int msg = 0; msg < 32; ++msg)
    {
      gaspi_rank_t sender;
      ASSERT(gaspi_passive_receive(0, 2048, &sender, sizeof(*data), GASPI_BLOCK));
      assert(sender == (rank + nprocs - 1) % nprocs);
      assert(data[2048 / sizeof(*data)] == round * 32 + msg + 1);
    }
    ASSERT(gaspi_barrier(GASPI_GROUP_ALL, GASPI_BLOCK));
  }
  data[512] = 0xdeadbeef;
  data[513] = 0xdeadbeef;
  ASSERT(gaspi_passive_send(0, 0, peer, 2 * sizeof(*data), GASPI_BLOCK));
  gaspi_rank_t sender;
  EXPECT_FAIL_WITH(gaspi_passive_receive(0, 2048, &sender, sizeof(*data),
                                        GASPI_BLOCK), GASPI_ERROR);
  assert(data[512] == 0xdeadbeef && data[513] == 0xdeadbeef);
  ASSERT(gaspi_barrier(GASPI_GROUP_ALL, GASPI_BLOCK));
  ASSERT(gaspi_proc_term(GASPI_BLOCK));
  return EXIT_SUCCESS;
}
