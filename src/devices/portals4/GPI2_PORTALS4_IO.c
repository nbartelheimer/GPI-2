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
#include "GPI2_PORTALS4.h"

/* Experimental notify function */

gaspi_return_t
pgaspi_dev_notify_wait(gaspi_context_t const* const gctx,
                       gaspi_notification_object_t* const notifications,
                       const gaspi_number_t num,
                       const gaspi_timeout_t timeout_ms)
{
  gaspi_portals4_ctx* const dev = gctx->device->ctx;
  int ret = -1;
  unsigned idx = 0;
  ptl_event_t event;

  for(int i = 0; i < num; ++i)
  {
    ret = PtlEQPoll(&dev->notification_eq_h, 1, timeout_ms, &event, &idx);
    if((ret != PTL_OK) || (event.ni_fail_type != PTL_NI_OK))
    {
      return GASPI_ERROR;
    }
    notifications[i].segment_id = (gaspi_segment_id_t)event.hdr_data;
    notifications[i].changed_bytes = (gaspi_size_t)event.mlength;
  }
  return GASPI_SUCCESS;
}

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
  int ret;
  unsigned int which;
  ptl_ct_event_t ce;
  gaspi_portals4_ctx* const dev = (gaspi_portals4_ctx*)gctx->device->ctx;
  const ptl_size_t nr =
      (ptl_size_t)gctx->ne_count_c[queue] + dev->comm_notif_ct_cnt[queue];

  ret =
      PtlCTPoll(&dev->comm_notif_ct_h[queue], &nr, 1, timeout_ms, &ce, &which);

  if(ret != PTL_OK)
  {
    GASPI_DEBUG_PRINT_ERROR("PtlCTPoll failed with %d", ret);
    return ret == PTL_CT_NONE_REACHED ? GASPI_TIMEOUT : GASPI_ERROR;
  }

  gctx->ne_count_c[queue] = 0;
  dev->comm_notif_ct_cnt[queue] = ce.success;
  return GASPI_SUCCESS;
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
      PtlCTPoll(&dev->comm_notif_ct_h[queue], &nr, 1, timeout_ms, &ce, &which);

  if(ret != PTL_OK)
  {
    GASPI_DEBUG_PRINT_ERROR("PtlCTPoll failed with %d", ret);
    return ret == PTL_CT_NONE_REACHED ? GASPI_TIMEOUT : GASPI_ERROR;
  }

  if(ce.failure > 0)
  {
    for(int i = 0; i < ce.failure; ++i)
    {
      ptl_event_t event;
      unsigned int event_idx;
      PtlEQPoll(&dev->comm_notif_err_eq_h[queue], 1, timeout_ms, &event,
                &event_idx);
      gaspi_rank_t rank = (gaspi_rank_t)(uintptr_t)event.user_ptr;
      gctx->state_vec[queue][rank] = GASPI_STATE_CORRUPT;
    }
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
  ptl_size_t message_count = num;

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
      message_count--;
      continue;
    }

    local_offset =
        gctx->rrmd[segment_id_local[i]][gctx->rank].data.addr + offset_local[i];
    remote_offset =
        gctx->rrmd[segment_id_remote[i]][rank].data.addr + offset_remote[i];

    ret = PtlPut(dev->comm_notif_md_h[queue], local_offset, size[i],
                 PORTALS4_ACK_TYPE, dev->remote_info[rank].phys_address,
                 dev->data_pt_idx, 0, remote_offset, (void*)(uintptr_t)rank, 0);

    if(ret != GASPI_SUCCESS)
    {
      return ret;
    }
  }

  ret = PtlEndBundle(dev->ni_h);
  if(ret != PTL_OK)
  {
    GASPI_DEBUG_PRINT_ERROR("PtlEndBundle failed with %d", ret);
    return GASPI_ERROR;
  }

  gctx->ne_count_c[queue] += message_count;
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
  ptl_size_t message_count = num;

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
      message_count--;
      continue;
    }

    local_offset =
        gctx->rrmd[segment_id_local[i]][gctx->rank].data.addr + offset_local[i];
    remote_offset =
        gctx->rrmd[segment_id_remote[i]][rank].data.addr + offset_remote[i];

    ret = PtlGet(dev->comm_notif_md_h[queue], local_offset, size[i],
                 dev->remote_info[rank].phys_address, dev->data_pt_idx, 0,
                 remote_offset, (void*)(uintptr_t)rank);

    if(ret != GASPI_SUCCESS)
    {
      return ret;
    }
  }

  ret = PtlEndBundle(dev->ni_h);
  if(ret != PTL_OK)
  {
    GASPI_DEBUG_PRINT_ERROR("PtlEndBundle failed with %d", ret);
    return GASPI_ERROR;
  }

  gctx->ne_count_c[queue] += message_count;
  return GASPI_SUCCESS;
}

gaspi_return_t
pgaspi_dev_notify(gaspi_context_t* const gctx,
                  const gaspi_segment_id_t segment_id_remote,
                  const gaspi_rank_t rank,
                  const gaspi_notification_id_t GASPI_UNUSED(notification_id),
                  const gaspi_notification_t GASPI_UNUSED(notification_value),
                  const gaspi_queue_id_t queue)
{
  int ret;
  gaspi_portals4_ctx* const dev = gctx->device->ctx;

  if(gctx->ne_count_c[queue] == gctx->config->queue_size_max)
  {
    return GASPI_QUEUE_FULL;
  }

  ret = PtlPut(dev->comm_notif_md_h[queue], 0, 0, PORTALS4_ACK_TYPE,
               dev->remote_info[rank].phys_address, dev->notification_pt_idx, 0,
               0, (void*)(uintptr_t)rank, (ptl_hdr_data_t)segment_id_remote);

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
    const gaspi_notification_id_t GASPI_UNUSED(notification_id),
    const gaspi_notification_t GASPI_UNUSED(notification_value),
    const gaspi_queue_id_t queue)
{
  int ret;
  gaspi_portals4_ctx* const dev = gctx->device->ctx;
  ptl_size_t local_offset =
      gctx->rrmd[segment_id_local][gctx->rank].data.addr + offset_local;
  ptl_size_t remote_offset =
      gctx->rrmd[segment_id_remote][rank].data.addr + offset_remote;

  if(gctx->ne_count_c[queue] == gctx->config->queue_size_max)
  {
    return GASPI_QUEUE_FULL;
  }

  ret = PtlPut(dev->comm_notif_md_h[queue], local_offset, size,
               PORTALS4_ACK_TYPE, dev->remote_info[rank].phys_address,
               dev->notification_pt_idx, 0, remote_offset,
               (void*)(uintptr_t)rank, (ptl_hdr_data_t)segment_id_remote);

  if(ret != GASPI_SUCCESS)
  {
    return ret;
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
  ptl_size_t message_count = num + 1;

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
      message_count--;
      continue;
    }

    local_offset =
        gctx->rrmd[segment_id_local[i]][gctx->rank].data.addr + offset_local[i];
    remote_offset =
        gctx->rrmd[segment_id_remote[i]][rank].data.addr + offset_remote[i];

    ret = PtlPut(dev->comm_notif_md_h[queue], local_offset, size[i],
                 PORTALS4_ACK_TYPE, dev->remote_info[rank].phys_address,
                 dev->data_pt_idx, 0, remote_offset, (void*)(uintptr_t)rank, 0);

    if(ret != GASPI_SUCCESS)
    {
      return ret;
    }
  }

  ret = PtlEndBundle(dev->ni_h);

  if(ret != PTL_OK)
  {
    GASPI_DEBUG_PRINT_ERROR("PtlEndBundle failed with %d", ret);
    return GASPI_ERROR;
  }

  ((gaspi_notification_t*)gctx->nsrc.notif_spc.buf)[notification_id] =
      notification_value;
  local_offset = gctx->nsrc.notif_spc.addr +
                 notification_id * sizeof(gaspi_notification_t);
  remote_offset = gctx->rrmd[segment_id_notification][rank].notif_spc.addr +
                  notification_id * sizeof(gaspi_notification_t);

  ret = PtlPut(dev->comm_notif_md_h[queue], local_offset,
               sizeof(gaspi_notification_t), PORTALS4_ACK_TYPE,
               dev->remote_info[rank].phys_address, dev->data_pt_idx, 0,
               remote_offset, (void*)(uintptr_t)rank, 0);

  if(ret != GASPI_SUCCESS)
  {
    return ret;
  }

  gctx->ne_count_c[queue] += message_count;
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
  int ret = -1;
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

  local_offset = gctx->rrmd[segment_id_local][gctx->rank].notif_spc.addr +
                 notification_id * sizeof(gaspi_notification_t);
  remote_offset = gctx->rrmd[segment_id_remote][rank].notif_spc.addr +
                  NOTIFICATIONS_SPACE_SIZE - sizeof(gaspi_notification_t);

  ret =
      PtlGet(dev->comm_notif_md_h[queue], local_offset,
             sizeof(gaspi_notification_t), dev->remote_info[rank].phys_address,
             dev->data_pt_idx, 0, remote_offset, (void*)(uintptr_t)rank);

  if(ret != PTL_OK)
  {
    GASPI_DEBUG_PRINT_ERROR("PtlGet failed with %d", ret);
    return GASPI_ERROR;
  }

  gctx->ne_count_c[queue] += 2;
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
  ptl_size_t message_count = num + 1;

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
      message_count--;
      continue;
    }

    local_offset =
        gctx->rrmd[segment_id_local[i]][gctx->rank].data.addr + offset_local[i];
    remote_offset =
        gctx->rrmd[segment_id_remote[i]][rank].data.addr + offset_remote[i];

    ret = PtlGet(dev->comm_notif_md_h[queue], local_offset, size[i],
                 dev->remote_info[rank].phys_address, dev->data_pt_idx, 0,
                 remote_offset, (void*)(uintptr_t)rank);

    if(ret != GASPI_SUCCESS)
    {
      return ret;
    }
  }

  ret = PtlEndBundle(dev->ni_h);

  if(ret != PTL_OK)
  {
    GASPI_DEBUG_PRINT_ERROR("PtlEndBundle failed with %d", ret);
    return GASPI_ERROR;
  }

  local_offset =
      gctx->rrmd[segment_id_notification][gctx->rank].notif_spc.addr +
      notification_id * sizeof(gaspi_notification_t);
  remote_offset = gctx->rrmd[segment_id_notification][rank].notif_spc.addr +
                  NOTIFICATIONS_SPACE_SIZE - sizeof(gaspi_notification_t);

  ret =
      PtlGet(dev->comm_notif_md_h[queue], local_offset,
             sizeof(gaspi_notification_t), dev->remote_info[rank].phys_address,
             dev->data_pt_idx, 0, remote_offset, (void*)(uintptr_t)rank);

  if(ret != PTL_OK)
  {
    GASPI_DEBUG_PRINT_ERROR("PtlGet failed with %d", ret);
    return GASPI_ERROR;
  }

  gctx->ne_count_c[queue] += message_count;
  return GASPI_SUCCESS;
}
