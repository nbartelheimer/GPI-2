#define _GNU_SOURCE
#include <test_utils.h>
#include <GASPI_Ext.h>
#include <GPI2.h>
#include <GPI2_PORTALS4.h>
#include <dlfcn.h>

static int fail_eq;
static int fail_md;
static int fail_le;
static int fail_put;
static int fail_get;
static int bundle_depth;

int PtlEQAlloc(ptl_handle_ni_t ni, ptl_size_t count, ptl_handle_eq_t *eq)
{
  if(fail_eq && --fail_eq == 0)
    return PTL_NO_SPACE;
  __typeof__(&PtlEQAlloc) real = dlsym(RTLD_NEXT, "PtlEQAlloc");
  assert(real);
  return real(ni, count, eq);
}

int PtlMDBind(ptl_handle_ni_t ni, const ptl_md_t *md, ptl_handle_md_t *handle)
{
  if(fail_md && --fail_md == 0)
    return PTL_NO_SPACE;
  __typeof__(&PtlMDBind) real = dlsym(RTLD_NEXT, "PtlMDBind");
  assert(real);
  return real(ni, md, handle);
}

int PtlLEAppend(ptl_handle_ni_t ni, ptl_pt_index_t pt, const ptl_le_t *le,
                ptl_list_t list, void *user, ptl_handle_le_t *handle)
{
  if(fail_le && --fail_le == 0)
    return PTL_NO_SPACE;
  __typeof__(&PtlLEAppend) real = dlsym(RTLD_NEXT, "PtlLEAppend");
  assert(real);
  return real(ni, pt, le, list, user, handle);
}

int PtlStartBundle(ptl_handle_ni_t ni)
{
  __typeof__(&PtlStartBundle) real = dlsym(RTLD_NEXT, "PtlStartBundle");
  assert(real);
  int ret = real(ni);
  if(ret == PTL_OK)
    bundle_depth++;
  return ret;
}

int PtlEndBundle(ptl_handle_ni_t ni)
{
  __typeof__(&PtlEndBundle) real = dlsym(RTLD_NEXT, "PtlEndBundle");
  assert(real);
  int ret = real(ni);
  if(ret == PTL_OK)
    bundle_depth--;
  return ret;
}

int PtlPut(ptl_handle_md_t md, ptl_size_t local, ptl_size_t length,
            ptl_ack_req_t ack, ptl_process_t peer, ptl_pt_index_t pt,
            ptl_match_bits_t bits, ptl_size_t remote, void *user,
            ptl_hdr_data_t hdr)
{
  if(fail_put && --fail_put == 0)
    return PTL_ARG_INVALID;
  __typeof__(&PtlPut) real = dlsym(RTLD_NEXT, "PtlPut");
  assert(real);
  return real(md, local, length, ack, peer, pt, bits, remote, user, hdr);
}

int PtlGet(ptl_handle_md_t md, ptl_size_t local, ptl_size_t length,
            ptl_process_t peer, ptl_pt_index_t pt, ptl_match_bits_t bits,
            ptl_size_t remote, void *user)
{
  if(fail_get && --fail_get == 0)
    return PTL_ARG_INVALID;
  __typeof__(&PtlGet) real = dlsym(RTLD_NEXT, "PtlGet");
  assert(real);
  return real(md, local, length, peer, pt, bits, remote, user);
}

static void check_pending(gaspi_number_t expected)
{
  gaspi_number_t pending;
  ASSERT(gaspi_queue_size(0, &pending));
  assert(pending == expected);
  assert(bundle_depth == 0);
  ASSERT(gaspi_wait(0, 5000));
}

int main(int argc, char **argv)
{
  TSUITE_INIT(argc, argv);
  for(int stage = 0; stage < 3; ++stage)
  {
    gaspi_config_t config;
    ASSERT(gaspi_config_get(&config));
    gaspi_context_t context = {0};
    context.config = &config;
    context.tnc = 1;
    context.num_queues = 1;
    fail_eq = stage == 0 ? 2 : 0;
    fail_md = stage == 1 ? 1 : 0;
    fail_le = stage == 2 ? 10 : 0;
    assert(pgaspi_dev_init_core(&context) == GASPI_ERROR);
    assert(context.device == NULL);
    assert(pgaspi_dev_cleanup_core(&context) == GASPI_SUCCESS);
  }
  ASSERT(gaspi_proc_init(GASPI_BLOCK));
  gaspi_rank_t rank, nprocs;
  ASSERT(gaspi_proc_rank(&rank));
  ASSERT(gaspi_proc_num(&nprocs));
  ASSERT(gaspi_segment_create(0, 128, GASPI_GROUP_ALL, GASPI_BLOCK,
                              GASPI_MEM_INITIALIZED));
  gaspi_rank_t peer = (rank + 1) % nprocs;
  gaspi_segment_id_t segments[3] = {0, 0, 0};
  gaspi_offset_t offsets[3] = {0, 8, 16};
  gaspi_size_t sizes[3] = {8, 8, 8};
  fail_put = 2;
  EXPECT_FAIL_WITH(gaspi_write_list(3, segments, offsets, peer, segments,
                                   offsets, sizes, 0, GASPI_BLOCK), GASPI_ERROR);
  check_pending(1);
  fail_get = 2;
  EXPECT_FAIL_WITH(gaspi_read_list(3, segments, offsets, peer, segments,
                                  offsets, sizes, 0, GASPI_BLOCK), GASPI_ERROR);
  check_pending(1);
  fail_put = 2;
  EXPECT_FAIL_WITH(gaspi_write_notify(0, 0, peer, 0, 0, 8, 0, 1, 0,
                                     GASPI_BLOCK), GASPI_ERROR);
  check_pending(1);
  fail_get = 2;
  EXPECT_FAIL_WITH(gaspi_read_notify(0, 0, peer, 0, 0, 8, 0, 0,
                                    GASPI_BLOCK), GASPI_ERROR);
  check_pending(1);
  gaspi_queue_id_t queue;
  fail_md = 1;
  EXPECT_FAIL(gaspi_queue_create(&queue, GASPI_BLOCK));
  ASSERT(gaspi_queue_create(&queue, GASPI_BLOCK));
  ASSERT(gaspi_queue_delete(queue));
  ASSERT(gaspi_write_list(3, segments, offsets, peer, segments, offsets,
                          sizes, 0, GASPI_BLOCK));
  check_pending(3);
  ASSERT(gaspi_barrier(GASPI_GROUP_ALL, GASPI_BLOCK));
  ASSERT(gaspi_proc_term(GASPI_BLOCK));
  return EXIT_SUCCESS;
}
