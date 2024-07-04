#include "PGASPI.h"
#include "GPI2.h"
#include "GPI2_Dev.h"
#include "GPI2_Utility.h"
#include "GASPI_Vector_access.h"

#include "GPI2_SN.h"

#pragma weak gaspi_vector_write = pgaspi_vector_write
gaspi_return_t
pgaspi_vector_write (const gaspi_segment_id_t segment_id_local,
              const gaspi_offset_t offset_local,
	            const gaspi_offset_t block_count,
	            const gaspi_offset_t block_size,
	            const gaspi_offset_t stride,
              const gaspi_rank_t rank,
              const gaspi_segment_id_t segment_id_remote,
              const gaspi_offset_t offset_remote,
              const gaspi_queue_id_t queue, 
              const gaspi_timeout_t timeout_ms)
{
  gaspi_context_t *const gctx = &glb_gaspi_ctx;

  GASPI_VERIFY_INIT ("gaspi_vector_write");
  GASPI_VERIFY_LOCAL_OFF (offset_local, segment_id_local, size);
  GASPI_VERIFY_REMOTE_OFF (offset_remote, segment_id_remote, rank, size);
  GASPI_VERIFY_QUEUE (queue);
  GASPI_VERIFY_COMM_SIZE (size, segment_id_local, segment_id_remote, rank,
                          GASPI_MIN_TSIZE_C, gctx->config->transfer_size_max);

  gaspi_return_t eret = GASPI_ERROR;

  if (GASPI_ENDPOINT_DISCONNECTED == gctx->ep_conn[rank].cstat)
  {
    eret = pgaspi_connect ((gaspi_rank_t) rank, timeout_ms);
    if (eret != GASPI_SUCCESS)
    {
      return eret;
    }
  }

  if (block_size == 0)
  {
    return GASPI_SUCCESS;
  }

  if (lock_gaspi_tout (&gctx->lockC[queue], timeout_ms))
  {
    return GASPI_TIMEOUT;
  }

  eret = pgaspi_dev_vector_write (gctx,
                           segment_id_local, offset_local, block_count,block_size,stride,rank,
                           segment_id_remote, offset_remote, queue);

  if (eret != GASPI_SUCCESS)
  {
    gctx->state_vec[queue][rank] = GASPI_STATE_CORRUPT;
    goto endL;
  }

  GPI2_STATS_INC_COUNT (GASPI_STATS_COUNTER_NUM_WRITE, 1);
  GPI2_STATS_INC_COUNT (GASPI_STATS_COUNTER_BYTES_WRITE, size);

endL:
  unlock_gaspi (&gctx->lockC[queue]);
  return eret;
}

#pragma weak gaspi_vector_write_notify = pgaspi_vector_write_notify
gaspi_return_t
pgaspi_vector_write_notify (const gaspi_segment_id_t segment_id_local,
                     const gaspi_offset_t offset_local,
		                 const gaspi_offset_t block_count,
		                 const gaspi_offset_t block_size,
		                 const gaspi_offset_t stride,
                     const gaspi_rank_t rank,
                     const gaspi_segment_id_t segment_id_remote,
                     const gaspi_offset_t offset_remote,
                     const gaspi_notification_id_t notification_id,
                     const gaspi_notification_t notification_value,
                     const gaspi_queue_id_t queue,
                     const gaspi_timeout_t timeout_ms)
{
  gaspi_context_t *const gctx = &glb_gaspi_ctx;

  GASPI_VERIFY_INIT ("gaspi_vector_write_notify");
  GASPI_VERIFY_LOCAL_OFF (offset_local, segment_id_local, size);
  GASPI_VERIFY_REMOTE_OFF (offset_remote, segment_id_remote, rank, size);
  GASPI_VERIFY_QUEUE (queue);
  GASPI_VERIFY_COMM_SIZE (size, segment_id_local, segment_id_remote, rank,
                          GASPI_MIN_TSIZE_C, gctx->config->transfer_size_max);

  if (notification_value == 0)
  {
    gaspi_printf ("Zero is not allowed as notification value.");
    return GASPI_ERR_INV_NOTIF_VAL;
  }

  gaspi_return_t eret = GASPI_ERROR;

  if (GASPI_ENDPOINT_DISCONNECTED == gctx->ep_conn[rank].cstat)
  {
    eret = pgaspi_connect ((gaspi_rank_t) rank, timeout_ms);
    if (eret != GASPI_SUCCESS)
    {
      return eret;
    }
  }

  if (block_size == 0)
  {
    return pgaspi_notify (segment_id_remote, rank,
                          notification_id, notification_value,
                          queue, timeout_ms);
  }

  if (lock_gaspi_tout (&gctx->lockC[queue], timeout_ms))
  {
    return GASPI_TIMEOUT;
  }

  eret = pgaspi_dev_vector_write_notify (gctx,
                                  segment_id_local, offset_local, block_count, block_size, stride, rank,
                                  segment_id_remote, offset_remote,
                                  notification_id, notification_value, queue);

  if (eret != GASPI_SUCCESS)
  {
    gctx->state_vec[queue][rank] = GASPI_STATE_CORRUPT;
    goto endL;
  }

  GPI2_STATS_INC_COUNT (GASPI_STATS_COUNTER_NUM_WRITE_NOT, 1);
  GPI2_STATS_INC_COUNT (GASPI_STATS_COUNTER_BYTES_WRITE, size);

endL:
  unlock_gaspi (&gctx->lockC[queue]);
  return eret;
}
