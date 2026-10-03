/*
Copyright (c) Goethe University Frankfurt MSQC - Niklas Bartelheimer
<bartelheimer@em.uni-frankfurt.de>, 2023-2026

This file is part of GPI-2.

GPI-2 is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License
version 3 as published by the Free Software Foundation.

GPI-2 is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GPI-2. If not, see <http://www.gnu.org/licenses/>.
*/
#include "GASPI.h"
#include "GPI2.h"
#include "GPI2_SEG.h"
#include "GPI2_PORTALS4.h"

/* Communication functions */
gaspi_return_t
pgaspi_dev_write(gaspi_context_t* const gctx,
                 const gaspi_segment_id_t segment_id_local,
                 const gaspi_offset_t offset_local, const gaspi_rank_t rank,
                 const gaspi_segment_id_t segment_id_remote,
                 const gaspi_offset_t offset_remote, const gaspi_size_t size,
                 const gaspi_queue_id_t queue)
{
  int ret;
  gaspi_portals4_ctx* const dev = gctx->device->ctx;
  const ptl_size_t local_offset =
      gctx->rrmd[segment_id_local][gctx->rank].data.addr + offset_local;
  const ptl_size_t remote_offset =
      gctx->rrmd[segment_id_remote][rank].data.addr + offset_remote;

  if(gctx->ne_count_c[queue] == gctx->config->queue_size_max)
  {
    return GASPI_QUEUE_FULL;
  }

  ret = PtlPut(dev->comm_notif_md_h[queue], local_offset, size,
               PORTALS4_ACK_TYPE, dev->remote_info[rank].phys_address,
               dev->data_pt_idx, 0, remote_offset, (void*)(uintptr_t)rank, 0);

  if(ret != PTL_OK)
  {
    GASPI_DEBUG_PRINT_ERROR("PtlPut failed with %d", ret);
    return GASPI_ERROR;
  }

  gctx->ne_count_c[queue]++;
  return GASPI_SUCCESS;
}

gaspi_return_t
pgaspi_dev_read(gaspi_context_t* const gctx,
                const gaspi_segment_id_t segment_id_local,
                const gaspi_offset_t offset_local, const gaspi_rank_t rank,
                const gaspi_segment_id_t segment_id_remote,
                const gaspi_offset_t offset_remote, const gaspi_size_t size,
                const gaspi_queue_id_t queue)
{
  int ret;
  gaspi_portals4_ctx* const dev = gctx->device->ctx;
  const ptl_size_t local_offset =
      gctx->rrmd[segment_id_local][gctx->rank].data.addr + offset_local;
  const ptl_size_t remote_offset =
      gctx->rrmd[segment_id_remote][rank].data.addr + offset_remote;

  if(gctx->ne_count_c[queue] == gctx->config->queue_size_max)
  {
    return GASPI_QUEUE_FULL;
  }

  ret = PtlGet(dev->comm_notif_md_h[queue], local_offset, size,
               dev->remote_info[rank].phys_address, dev->data_pt_idx, 0,
               remote_offset, (void*)(uintptr_t)rank);

  if(ret != PTL_OK)
  {
    GASPI_DEBUG_PRINT_ERROR("PtlGet failed with %d", ret);
    return GASPI_ERROR;
  }

  gctx->ne_count_c[queue]++;
  return GASPI_SUCCESS;
}

gaspi_return_t
pgaspi_dev_purge(gaspi_context_t* const gctx, const gaspi_queue_id_t queue,
                  const gaspi_timeout_t timeout_ms)
{
  return pgaspi_dev_wait(gctx, queue, timeout_ms);
}

gaspi_return_t
pgaspi_dev_wait(gaspi_context_t* const gctx, const gaspi_queue_id_t queue,
                const gaspi_timeout_t timeout_ms)
{
  int ret;
  ptl_ct_event_t ce;
  unsigned int which;
  gaspi_portals4_ctx* const dev = (gaspi_portals4_ctx*)gctx->device->ctx;
  const ptl_size_t nr =
      (ptl_size_t)gctx->ne_count_c[queue] + dev->comm_notif_ct_cnt[queue];

  ret =
      PtlCTPoll(&dev->comm_notif_ct_h[queue], &nr, 1,
                timeout_ms == GASPI_BLOCK ? PTL_TIME_FOREVER : timeout_ms,
                &ce, &which);

  if(ret != PTL_OK)
  {
    GASPI_DEBUG_PRINT_ERROR("PtlCTPoll failed with %d", ret);
    return ret == PTL_CT_NONE_REACHED ? GASPI_TIMEOUT : GASPI_ERROR;
  }

  if(ce.failure > 0)
  {
    pgaspi_portals4_errors(gctx, dev->comm_notif_err_eq_h[queue], queue);
    GASPI_DEBUG_PRINT_ERROR("Comm queue %d might be broken!", queue);
    return GASPI_ERROR;
  }

  gctx->ne_count_c[queue] = 0;
  dev->comm_notif_ct_cnt[queue] = ce.success;
  return GASPI_SUCCESS;
}

gaspi_return_t
pgaspi_dev_write_list(gaspi_context_t* const gctx, const gaspi_number_t num,
                      gaspi_segment_id_t* const segment_id_local,
                      gaspi_offset_t* const offset_local,
                      const gaspi_rank_t rank,
                      gaspi_segment_id_t* const segment_id_remote,
                      gaspi_offset_t* const offset_remote,
                      gaspi_size_t* const size, const gaspi_queue_id_t queue)
{
  int ret;
  gaspi_portals4_ctx* const dev = (gaspi_portals4_ctx*)gctx->device->ctx;
  ptl_size_t local_offset = 0;
  ptl_size_t remote_offset = 0;

  if(gctx->ne_count_c[queue] + num > gctx->config->queue_size_max)
  {
    return GASPI_QUEUE_FULL;
  }

  ret = PtlStartBundle(dev->ni_h);

  if(ret != PTL_OK)
  {
    GASPI_DEBUG_PRINT_ERROR("PtlBundleStart failed with %d", ret);
    return GASPI_ERROR;
  }

  for(gaspi_number_t i = 0; i < num; ++i)
  {
    if(size[i] == 0)
    {
      continue;
    }

    local_offset =
        gctx->rrmd[segment_id_local[i]][gctx->rank].data.addr + offset_local[i];
    remote_offset =
        gctx->rrmd[segment_id_remote[i]][rank].data.addr + offset_remote[i];

    ret = PtlPut(dev->comm_notif_md_h[queue], local_offset, size[i],
                 PORTALS4_ACK_TYPE, dev->remote_info[rank].phys_address,
                 dev->data_pt_idx, 0, remote_offset, (void*)(uintptr_t)rank, 0);

    if(ret != PTL_OK)
    {
      ret = PtlEndBundle(dev->ni_h);
      GASPI_DEBUG_PRINT_ERROR("Portals4 list operation failed, bundle end %d",
                              ret);
      return GASPI_ERROR;
    }
    gctx->ne_count_c[queue]++;
  }

  ret = PtlEndBundle(dev->ni_h);
  if(ret != PTL_OK)
  {
    GASPI_DEBUG_PRINT_ERROR("PtlEndBundle failed with %d", ret);
    return GASPI_ERROR;
  }

  return GASPI_SUCCESS;
}

gaspi_return_t
pgaspi_dev_read_list(gaspi_context_t* const gctx, const gaspi_number_t num,
                     gaspi_segment_id_t* const segment_id_local,
                     gaspi_offset_t* const offset_local,
                     const gaspi_rank_t rank,
                     gaspi_segment_id_t* const segment_id_remote,
                     gaspi_offset_t* const offset_remote,
                     gaspi_size_t* const size, const gaspi_queue_id_t queue)
{
  int ret;
  gaspi_portals4_ctx* const dev = (gaspi_portals4_ctx*)gctx->device->ctx;
  ptl_size_t local_offset = 0;
  ptl_size_t remote_offset = 0;

  if(gctx->ne_count_c[queue] + num > gctx->config->queue_size_max)
  {
    return GASPI_QUEUE_FULL;
  }

  ret = PtlStartBundle(dev->ni_h);
  if(ret != PTL_OK)
  {
    GASPI_DEBUG_PRINT_ERROR("PtlBundleStart failed with %d", ret);
    return GASPI_ERROR;
  }

  for(gaspi_number_t i = 0; i < num; ++i)
  {
    if(size[i] == 0)
    {
      continue;
    }

    local_offset =
        gctx->rrmd[segment_id_local[i]][gctx->rank].data.addr + offset_local[i];
    remote_offset =
        gctx->rrmd[segment_id_remote[i]][rank].data.addr + offset_remote[i];

    ret = PtlGet(dev->comm_notif_md_h[queue], local_offset, size[i],
                 dev->remote_info[rank].phys_address, dev->data_pt_idx, 0,
                 remote_offset, (void*)(uintptr_t)rank);

    if(ret != PTL_OK)
    {
      ret = PtlEndBundle(dev->ni_h);
      GASPI_DEBUG_PRINT_ERROR("Portals4 list operation failed, bundle end %d",
                              ret);
      return GASPI_ERROR;
    }
    gctx->ne_count_c[queue]++;
  }

  ret = PtlEndBundle(dev->ni_h);
  if(ret != PTL_OK)
  {
    GASPI_DEBUG_PRINT_ERROR("PtlEndBundle failed with %d", ret);
    return GASPI_ERROR;
  }

  return GASPI_SUCCESS;
}

gaspi_return_t
pgaspi_dev_notify(gaspi_context_t* const gctx,
                  const gaspi_segment_id_t segment_id_remote,
                  const gaspi_rank_t rank,
                  const gaspi_notification_id_t notification_id,
                  const gaspi_notification_t notification_value,
                  const gaspi_queue_id_t queue)
{
  int ret;
  gaspi_portals4_ctx* const dev = gctx->device->ctx;
  const ptl_size_t local_offset = (ptl_size_t)&notification_value;
  const ptl_size_t remote_offset =
      gctx->rrmd[segment_id_remote][rank].notif_spc.addr +
      notification_id * sizeof(gaspi_notification_t);

  if(gctx->ne_count_c[queue] == gctx->config->queue_size_max)
  {
    return GASPI_QUEUE_FULL;
  }

  ret = PtlPut(dev->comm_notif_md_h[queue], local_offset,
               sizeof(gaspi_notification_t), PORTALS4_ACK_TYPE,
               dev->remote_info[rank].phys_address, dev->data_pt_idx, 0,
               remote_offset, (void*)(uintptr_t)rank, 0);

  if(ret != PTL_OK)
  {
    GASPI_DEBUG_PRINT_ERROR("PtlPut failed with %d", ret);
    return GASPI_ERROR;
  }

  gctx->ne_count_c[queue]++;
  return GASPI_SUCCESS;
}

gaspi_return_t
pgaspi_dev_write_notify(
    gaspi_context_t* const gctx, const gaspi_segment_id_t segment_id_local,
    const gaspi_offset_t offset_local, const gaspi_rank_t rank,
    const gaspi_segment_id_t segment_id_remote,
    const gaspi_offset_t offset_remote, const gaspi_size_t size,
    const gaspi_notification_id_t notification_id,
    const gaspi_notification_t notification_value, const gaspi_queue_id_t queue)
{
  int ret;
  gaspi_portals4_ctx* const dev = gctx->device->ctx;
  ptl_size_t local_offset =
      gctx->rrmd[segment_id_local][gctx->rank].data.addr + offset_local;
  ptl_size_t remote_offset =
      gctx->rrmd[segment_id_remote][rank].data.addr + offset_remote;

  if(gctx->ne_count_c[queue] + 2 > gctx->config->queue_size_max)
  {
    return GASPI_QUEUE_FULL;
  }

  ret = PtlPut(dev->comm_notif_md_h[queue], local_offset, size,
               PORTALS4_ACK_TYPE, dev->remote_info[rank].phys_address,
               dev->data_pt_idx, 0, remote_offset, (void*)(uintptr_t)rank, 0);

  if(ret != PTL_OK)
  {
    return GASPI_ERROR;
  }

  gctx->ne_count_c[queue]++;
  ptl_ct_event_t completed;
  ret = PtlCTWait(dev->comm_notif_ct_h[queue],
                  dev->comm_notif_ct_cnt[queue] + gctx->ne_count_c[queue],
                  &completed);
  if(ret != PTL_OK || completed.failure)
  {
    pgaspi_portals4_errors(gctx, dev->comm_notif_err_eq_h[queue], queue);
    return GASPI_ERROR;
  }

  local_offset = (ptl_size_t)&notification_value;
  remote_offset = gctx->rrmd[segment_id_remote][rank].notif_spc.addr +
                  notification_id * sizeof(gaspi_notification_t);

  ret = PtlPut(dev->comm_notif_md_h[queue], local_offset,
               sizeof(gaspi_notification_t), PORTALS4_ACK_TYPE,
               dev->remote_info[rank].phys_address, dev->data_pt_idx, 0,
               remote_offset, (void*)(uintptr_t)rank, 0);

  if(ret != PTL_OK)
  {
    return GASPI_ERROR;
  }

  gctx->ne_count_c[queue]++;
  return GASPI_SUCCESS;
}

gaspi_return_t
pgaspi_dev_write_list_notify(
    gaspi_context_t* const gctx, const gaspi_number_t num,
    gaspi_segment_id_t* const segment_id_local,
    gaspi_offset_t* const offset_local, const gaspi_rank_t rank,
    gaspi_segment_id_t* const segment_id_remote,
    gaspi_offset_t* const offset_remote, gaspi_size_t* const size,
    const gaspi_segment_id_t segment_id_notification,
    const gaspi_notification_id_t notification_id,
    const gaspi_notification_t notification_value, const gaspi_queue_id_t queue)
{
  int ret;
  gaspi_portals4_ctx* const dev = gctx->device->ctx;
  ptl_size_t local_offset = 0;
  ptl_size_t remote_offset = 0;

  if(gctx->ne_count_c[queue] + num + 1 > gctx->config->queue_size_max)
  {
    return GASPI_QUEUE_FULL;
  }

  ret = PtlStartBundle(dev->ni_h);
  if(ret != PTL_OK)
  {
    GASPI_DEBUG_PRINT_ERROR("PtlBundleStart failed with %d", ret);
    return GASPI_ERROR;
  }

  for(gaspi_number_t i = 0; i < num; ++i)
  {
    if(size[i] == 0)
    {
      continue;
    }

    local_offset =
        gctx->rrmd[segment_id_local[i]][gctx->rank].data.addr + offset_local[i];
    remote_offset =
        gctx->rrmd[segment_id_remote[i]][rank].data.addr + offset_remote[i];

    ret = PtlPut(dev->comm_notif_md_h[queue], local_offset, size[i],
                 PORTALS4_ACK_TYPE, dev->remote_info[rank].phys_address,
                 dev->data_pt_idx, 0, remote_offset, (void*)(uintptr_t)rank, 0);

    if(ret != PTL_OK)
    {
      ret = PtlEndBundle(dev->ni_h);
      GASPI_DEBUG_PRINT_ERROR("Portals4 list operation failed, bundle end %d",
                              ret);
      return GASPI_ERROR;
    }
    gctx->ne_count_c[queue]++;
  }

  ret = PtlEndBundle(dev->ni_h);

  if(ret != PTL_OK)
  {
    GASPI_DEBUG_PRINT_ERROR("PtlEndBundle failed with %d", ret);
    return GASPI_ERROR;
  }

  ptl_ct_event_t completed;
  ret = PtlCTWait(dev->comm_notif_ct_h[queue],
                  dev->comm_notif_ct_cnt[queue] + gctx->ne_count_c[queue],
                  &completed);
  if(ret != PTL_OK || completed.failure)
  {
    pgaspi_portals4_errors(gctx, dev->comm_notif_err_eq_h[queue], queue);
    return GASPI_ERROR;
  }

  local_offset = (ptl_size_t)&notification_value;
  remote_offset = gctx->rrmd[segment_id_notification][rank].notif_spc.addr +
                  notification_id * sizeof(gaspi_notification_t);

  ret = PtlPut(dev->comm_notif_md_h[queue], local_offset,
               sizeof(gaspi_notification_t), PORTALS4_ACK_TYPE,
               dev->remote_info[rank].phys_address, dev->data_pt_idx, 0,
               remote_offset, (void*)(uintptr_t)rank, 0);

  if(ret != PTL_OK)
  {
    return GASPI_ERROR;
  }

  gctx->ne_count_c[queue]++;
  return GASPI_SUCCESS;
}

gaspi_return_t
pgaspi_dev_read_notify(
    gaspi_context_t* const gctx, const gaspi_segment_id_t segment_id_local,
    const gaspi_offset_t offset_local, const gaspi_rank_t rank,
    const gaspi_segment_id_t segment_id_remote,
    const gaspi_offset_t offset_remote, const gaspi_size_t size,
    const gaspi_notification_id_t notification_id, const gaspi_queue_id_t queue)
{
  int ret;
  gaspi_portals4_ctx* const dev = gctx->device->ctx;
  ptl_size_t local_offset =
      gctx->rrmd[segment_id_local][gctx->rank].data.addr + offset_local;
  ptl_size_t remote_offset =
      gctx->rrmd[segment_id_remote][rank].data.addr + offset_remote;

  if(gctx->ne_count_c[queue] + 2 > gctx->config->queue_size_max)
  {
    return GASPI_QUEUE_FULL;
  }

  ret = PtlGet(dev->comm_notif_md_h[queue], local_offset, size,
               dev->remote_info[rank].phys_address, dev->data_pt_idx, 0,
               remote_offset, (void*)(uintptr_t)rank);

  if(ret != PTL_OK)
  {
    GASPI_DEBUG_PRINT_ERROR("PtlGet failed with %d", ret);
    return GASPI_ERROR;
  }

  gctx->ne_count_c[queue]++;
  ptl_ct_event_t completed;
  ret = PtlCTWait(dev->comm_notif_ct_h[queue],
                  dev->comm_notif_ct_cnt[queue] + gctx->ne_count_c[queue],
                  &completed);
  if(ret != PTL_OK || completed.failure)
  {
    pgaspi_portals4_errors(gctx, dev->comm_notif_err_eq_h[queue], queue);
    return GASPI_ERROR;
  }

  local_offset = gctx->rrmd[segment_id_local][gctx->rank].notif_spc.addr +
                 notification_id * sizeof(gaspi_notification_t);
  remote_offset = gctx->rrmd[segment_id_remote][rank].notif_spc.addr +
                  pgaspi_notifications_space_size() - sizeof(gaspi_notification_t);

  ret =
      PtlGet(dev->comm_notif_md_h[queue], local_offset,
             sizeof(gaspi_notification_t), dev->remote_info[rank].phys_address,
             dev->data_pt_idx, 0, remote_offset, (void*)(uintptr_t)rank);

  if(ret != PTL_OK)
  {
    GASPI_DEBUG_PRINT_ERROR("PtlGet failed with %d", ret);
    return GASPI_ERROR;
  }

  gctx->ne_count_c[queue]++;
  return GASPI_SUCCESS;
}

gaspi_return_t
pgaspi_dev_read_list_notify(
    gaspi_context_t* const gctx, const gaspi_number_t num,
    gaspi_segment_id_t* const segment_id_local,
    gaspi_offset_t* const offset_local, const gaspi_rank_t rank,
    gaspi_segment_id_t* const segment_id_remote,
    gaspi_offset_t* const offset_remote, gaspi_size_t* const size,
    const gaspi_segment_id_t segment_id_notification,
    const gaspi_notification_id_t notification_id, const gaspi_queue_id_t queue)
{
  int ret;
  gaspi_portals4_ctx* const dev = gctx->device->ctx;
  ptl_size_t local_offset = 0;
  ptl_size_t remote_offset = 0;

  if(gctx->ne_count_c[queue] + num + 1 > gctx->config->queue_size_max)
  {
    return GASPI_QUEUE_FULL;
  }

  ret = PtlStartBundle(dev->ni_h);

  if(ret != PTL_OK)
  {
    GASPI_DEBUG_PRINT_ERROR("PtlBundleStart failed with %d", ret);
    return GASPI_ERROR;
  }

  for(gaspi_number_t i = 0; i < num; ++i)
  {
    if(size[i] == 0)
    {
      continue;
    }

    local_offset =
        gctx->rrmd[segment_id_local[i]][gctx->rank].data.addr + offset_local[i];
    remote_offset =
        gctx->rrmd[segment_id_remote[i]][rank].data.addr + offset_remote[i];

    ret = PtlGet(dev->comm_notif_md_h[queue], local_offset, size[i],
                 dev->remote_info[rank].phys_address, dev->data_pt_idx, 0,
                 remote_offset, (void*)(uintptr_t)rank);

    if(ret != PTL_OK)
    {
      ret = PtlEndBundle(dev->ni_h);
      GASPI_DEBUG_PRINT_ERROR("Portals4 list operation failed, bundle end %d",
                              ret);
      return GASPI_ERROR;
    }
    gctx->ne_count_c[queue]++;
  }

  ret = PtlEndBundle(dev->ni_h);

  if(ret != PTL_OK)
  {
    GASPI_DEBUG_PRINT_ERROR("PtlEndBundle failed with %d", ret);
    return GASPI_ERROR;
  }

  ptl_ct_event_t completed;
  ret = PtlCTWait(dev->comm_notif_ct_h[queue],
                  dev->comm_notif_ct_cnt[queue] + gctx->ne_count_c[queue],
                  &completed);
  if(ret != PTL_OK || completed.failure)
  {
    pgaspi_portals4_errors(gctx, dev->comm_notif_err_eq_h[queue], queue);
    return GASPI_ERROR;
  }

  local_offset =
      gctx->rrmd[segment_id_notification][gctx->rank].notif_spc.addr +
      notification_id * sizeof(gaspi_notification_t);
  remote_offset = gctx->rrmd[segment_id_notification][rank].notif_spc.addr +
                  pgaspi_notifications_space_size() - sizeof(gaspi_notification_t);

  ret =
      PtlGet(dev->comm_notif_md_h[queue], local_offset,
             sizeof(gaspi_notification_t), dev->remote_info[rank].phys_address,
             dev->data_pt_idx, 0, remote_offset, (void*)(uintptr_t)rank);

  if(ret != PTL_OK)
  {
    GASPI_DEBUG_PRINT_ERROR("PtlGet failed with %d", ret);
    return GASPI_ERROR;
  }

  gctx->ne_count_c[queue]++;
  return GASPI_SUCCESS;
}
